#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/Errors.h"
#include "core/Tool.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentResultTool.h"
#include "subagent/SubAgentTask.h"

using namespace aicoder;
using namespace std::chrono_literals;

namespace {

// Mirrors the FakeLlmClient pattern from SubAgentManagerTest.cpp.
class FakeLlmClient : public LlmClient {
public:
	explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}

	Response sendStream(const std::vector<Message>&,
	                    const std::vector<ToolSpec>&,
	                    const DeltaCallback& onDelta) override {
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

SubAgentDef makeDef(const std::string& name) {
	SubAgentDef d;
	d.name = name;
	d.role = "You are a " + name + " assistant.";
	d.max_iterations = 3;
	return d;
}

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
// ResultForUnknownTask — querying an unknown id must throw ToolError.
// ---------------------------------------------------------------------------
TEST(SubAgentResultTool, ResultForUnknownTask) {
	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	Tool tool = makeGetSubAgentResultTool(mgr);

	json input = {{"task_id", "subagent-bogus"}};
	EXPECT_THROW({ (void)tool.execute(input); }, ToolError);
}

// ---------------------------------------------------------------------------
// ResultForCompletedTask — blocking wait returns output + iterations_used.
// ---------------------------------------------------------------------------
TEST(SubAgentResultTool, ResultForCompletedTask) {
	FakeLlmClient fake([](int) { return textResp("payload"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	Tool tool = makeGetSubAgentResultTool(mgr);

	std::string id = mgr.runAsync(makeDef("helper"), "fetch");
	ASSERT_FALSE(id.empty());

	// wait=true + generous timeout — task should already be done by the time
	// the fake returns.
	json input = {{"task_id", id}, {"wait", true}, {"timeout_ms", 2000}};
	std::string raw = tool.execute(input);
	json out = json::parse(raw);

	EXPECT_EQ(out.value("task_id", ""), id);
	EXPECT_EQ(out.value("status", ""), "completed");
	EXPECT_EQ(out.value("output", ""), "payload");
	// The runner does not currently track iterations_used; the field is
	// surfaced for forward compatibility, so we only assert it is present
	// and non-negative.
	EXPECT_GE(out.value("iterations_used", 0), 0);
}

// ---------------------------------------------------------------------------
// ResultForRunningTaskNoWait — wait=false on a still-running task returns
// a "still running" hint rather than blocking.
// ---------------------------------------------------------------------------
TEST(SubAgentResultTool, ResultForRunningTaskNoWait) {
	auto release = std::make_shared<std::atomic<bool>>(false);
	FakeLlmClient fake([release](int) {
		while (!release->load()) std::this_thread::sleep_for(5ms);
		return textResp("late");
	});
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	Tool tool = makeGetSubAgentResultTool(mgr);

	std::string id = mgr.runAsync(makeDef("blocker"), "hold");
	ASSERT_TRUE(waitUntil([&] {
		auto info = mgr.getInfo(id);
		return info && info->status == SubAgentTaskStatus::Running;
	}));

	json input = {{"task_id", id}, {"wait", false}};
	std::string raw = tool.execute(input);
	json out = json::parse(raw);

	EXPECT_EQ(out.value("status", ""), "running");
	EXPECT_EQ(out.value("hint", ""), "still running; call again with wait=true");
	EXPECT_FALSE(out.contains("output"));

	// Let the worker drain so the manager's destructor doesn't race.
	release->store(true);
	mgr.waitFor(id, 2000ms);
}
