#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/AgentLoop.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentRunner.h"
#include "subagent/SubAgentTask.h"

using namespace aicoder;
using namespace std::chrono_literals;

namespace {

// Reuses the FakeLlmClient pattern from SubAgentRunnerTest.cpp / AgentLoopTest.cpp.
class FakeLlmClient : public LlmClient {
public:
	explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}

	Response sendStream(const std::vector<Message>& msgs,
	                    const std::vector<ToolSpec>&,
	                    const DeltaCallback& onDelta) override {
		(void)msgs;
		Response r = fn_(calls_.fetch_add(1));
		std::string txt = assistantText(r.assistant_message);
		if (!txt.empty() && onDelta) onDelta(StreamDelta{txt, ""});
		return r;
	}

	std::atomic<int> calls_{0};

private:
	std::function<Response(int)> fn_;
};

Response textResp(std::string t) {
	return Response{Message{Role::Assistant, {TextBlock{std::move(t)}}}, "", "stop"};
}

SubAgentDef makeDef(const std::string& name,
                    const std::vector<std::string>& tools = {},
                    int maxIter = 3) {
	SubAgentDef d;
	d.name = name;
	d.role = "You are a " + name + " assistant.";
	d.tools = tools;
	d.max_iterations = maxIter;
	return d;
}

// Helper: spin-wait up to `timeout` for `pred()` to become true.
// Avoids flaky sleeps in tests that race with the background thread.
template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds timeout = 2000ms) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred()) return true;
		std::this_thread::sleep_for(5ms);
	}
	return pred();
}

}  // namespace

// ---------------------------------------------------------------------------
// AsyncHappyPath — runAsync returns id; waitFor returns Completed; output ok.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, AsyncHappyPath) {
	FakeLlmClient fake([](int) { return textResp("done from sub"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg, {}, /*bgThreads=*/2);

	std::string id = mgr.runAsync(makeDef("worker"), "do a thing");
	EXPECT_FALSE(id.empty());

	SubAgentTaskInfo info = mgr.waitFor(id, 2000ms);
	EXPECT_EQ(info.status, SubAgentTaskStatus::Completed);
	EXPECT_EQ(info.output, "done from sub");
	EXPECT_EQ(info.agent_name, "worker");
	EXPECT_EQ(info.task, "do a thing");
	// At least one LLM round-trip happened.
	EXPECT_GT(fake.calls_.load(), 0);
}

// ---------------------------------------------------------------------------
// AsyncBackgroundReturnsImmediately — runAsync returns fast even with slow LLM.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, AsyncBackgroundReturnsImmediately) {
	FakeLlmClient fake([](int) {
		std::this_thread::sleep_for(200ms);
		return textResp("slow result");
	});
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	auto t0 = std::chrono::steady_clock::now();
	std::string id = mgr.runAsync(makeDef("slow"), "wait");
	auto elapsed = std::chrono::steady_clock::now() - t0;

	// Submission itself should be near-instant; allow a fat budget for CI noise.
	EXPECT_LT(elapsed, 50ms);
	EXPECT_FALSE(id.empty());

	// Drain so the test doesn't race with the background thread on shutdown.
	SubAgentTaskInfo info = mgr.waitFor(id, 2000ms);
	EXPECT_EQ(info.status, SubAgentTaskStatus::Completed);
}

// ---------------------------------------------------------------------------
// GetInfoBeforeCompletion — during execution, getInfo shows Running.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, GetInfoBeforeCompletion) {
	auto release = std::make_shared<std::atomic<bool>>(false);
	FakeLlmClient fake([release](int) {
		// Block until the test has had a chance to call getInfo.
		while (!release->load()) std::this_thread::sleep_for(5ms);
		return textResp("released");
	});
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	std::string id = mgr.runAsync(makeDef("blocker"), "hold");

	ASSERT_TRUE(waitUntil([&] {
		auto info = mgr.getInfo(id);
		return info && info->status == SubAgentTaskStatus::Running;
	}));

	auto running = mgr.getInfo(id);
	ASSERT_TRUE(running.has_value());
	EXPECT_EQ(running->status, SubAgentTaskStatus::Running);

	release->store(true);
	SubAgentTaskInfo done = mgr.waitFor(id, 2000ms);
	EXPECT_EQ(done.status, SubAgentTaskStatus::Completed);
}

// ---------------------------------------------------------------------------
// CancelCascadesToBackground — cancel before runAsync, status becomes Cancelled.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, CancelCascadesToBackground) {
	FakeLlmClient fake([](int) { return textResp("never seen"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	auto cancel = std::make_shared<std::atomic<bool>>(true);
	mgr.setCancelToken(cancel);

	std::string id = mgr.runAsync(makeDef("doomed"), "go");

	SubAgentTaskInfo info = mgr.waitFor(id, 2000ms);
	EXPECT_EQ(info.status, SubAgentTaskStatus::Cancelled);
}

// ---------------------------------------------------------------------------
// OutputTruncatedAt100K — 200K-char result is capped + marker appended.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, OutputTruncatedAt100K) {
	std::string huge(200000, 'a');
	FakeLlmClient fake([huge](int) { return textResp(huge); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	std::string id = mgr.runAsync(makeDef("verbose"), "spew");
	SubAgentTaskInfo info = mgr.waitFor(id, 2000ms);

	EXPECT_EQ(info.status, SubAgentTaskStatus::Completed);
	EXPECT_LE(info.output.size(), kSubAgentMaxOutputChars + 64);
	EXPECT_GE(info.output.size(), kSubAgentMaxOutputChars);
	EXPECT_NE(info.output.find("truncated"), std::string::npos);
}

// ---------------------------------------------------------------------------
// UnknownTaskId — getInfo returns nullopt, waitFor throws.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, UnknownTaskId) {
	FakeLlmClient fake([](int) { return textResp("x"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	EXPECT_FALSE(mgr.getInfo("subagent-bogus").has_value());
	EXPECT_THROW(mgr.waitFor("subagent-bogus", 10ms), std::out_of_range);
}

// ---------------------------------------------------------------------------
// SyncStillWorks — runSync returns the output inline.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, SyncStillWorks) {
	FakeLlmClient fake([](int) { return textResp("inline answer"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	SubAgentResult r = mgr.runSync(makeDef("sync"), "go");
	EXPECT_EQ(r.output, "inline answer");
	EXPECT_FALSE(r.was_cancelled);
}

// ---------------------------------------------------------------------------
// ListTasksIncludesAll — runAsync twice, listTasks returns both ids.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, ListTasksIncludesAll) {
	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	std::string a = mgr.runAsync(makeDef("a"), "1");
	std::string b = mgr.runAsync(makeDef("b"), "2");

	auto ids = mgr.listTasks();
	EXPECT_EQ(ids.size(), 2u);
	EXPECT_NE(std::find(ids.begin(), ids.end(), a), ids.end());
	EXPECT_NE(std::find(ids.begin(), ids.end(), b), ids.end());

	// Drain so the manager's destructor doesn't race the workers.
	mgr.waitFor(a, 2000ms);
	mgr.waitFor(b, 2000ms);
}

// ---------------------------------------------------------------------------
// ShutdownCancelsRunning — start a slow task, shutdown, status -> Cancelled.
// ---------------------------------------------------------------------------
TEST(SubAgentManager, ShutdownCancelsRunning) {
	auto release = std::make_shared<std::atomic<bool>>(false);
	FakeLlmClient fake([release](int) {
		while (!release->load()) std::this_thread::sleep_for(5ms);
		return textResp("late");
	});
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	// Cancel token must be set BEFORE runAsync so the worker reads the shared
	// state; shutdown() flips it but the running fake also needs to unblock.
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	mgr.setCancelToken(cancel);

	std::string id = mgr.runAsync(makeDef("forever"), "wait");

	// Let the worker actually enter Running before we shut down.
	ASSERT_TRUE(waitUntil([&] {
		auto info = mgr.getInfo(id);
		return info && info->status == SubAgentTaskStatus::Running;
	}));

	mgr.shutdown();
	// Unblock the fake so the worker thread can exit cleanly.
	release->store(true);

	auto info = mgr.getInfo(id);
	ASSERT_TRUE(info.has_value());
	EXPECT_EQ(info->status, SubAgentTaskStatus::Cancelled);
}
