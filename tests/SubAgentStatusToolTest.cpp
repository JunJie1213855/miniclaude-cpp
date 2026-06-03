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
#include "subagent/SubAgentStatusTool.h"
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
// StatusForUnknownTask — querying an unknown id must throw ToolError.
// ---------------------------------------------------------------------------
TEST(SubAgentStatusTool, StatusForUnknownTask) {
	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	Tool tool = makeGetSubAgentStatusTool(mgr);

	json input = {{"task_id", "subagent-bogus"}};
	EXPECT_THROW({ (void)tool.execute(input); }, ToolError);
}

// ---------------------------------------------------------------------------
// StatusForKnownTask — running task eventually reports "completed" + agent.
// ---------------------------------------------------------------------------
TEST(SubAgentStatusTool, StatusForKnownTask) {
	FakeLlmClient fake([](int) { return textResp("worker done"); });
	ToolRegistry reg;
	SubAgentRegistry agentReg;
	SubAgentManager mgr(fake, reg, agentReg);

	Tool tool = makeGetSubAgentStatusTool(mgr);

	std::string id = mgr.runAsync(makeDef("worker"), "do it");
	ASSERT_FALSE(id.empty());

	// Drain to terminal so the status query reflects the final state.
	SubAgentTaskInfo info = mgr.waitFor(id, 2000ms);
	ASSERT_EQ(info.status, SubAgentTaskStatus::Completed);

	std::string raw = tool.execute(json{{"task_id", id}});
	json out = json::parse(raw);

	EXPECT_EQ(out.value("task_id", ""), id);
	EXPECT_EQ(out.value("status", ""), "completed");
	EXPECT_EQ(out.value("agent", ""), "worker");
	EXPECT_TRUE(out.value("finished", false));
	EXPECT_GE(out.value("elapsed_ms", 0), 0);
}
