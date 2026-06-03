#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/Json.h"
#include "core/Tool.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentTask.h"
#include "subagent/SubAgentTool.h"

using namespace aicoder;
using namespace std::chrono_literals;

namespace fs = std::filesystem;

namespace {

// Minimal FakeLlmClient — same pattern as SubAgentToolTest.cpp / SubAgentManagerTest.cpp.
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

fs::path scratch() {
	fs::path d = fs::temp_directory_path() / "aicoder_subagenttool_async_test";
	fs::remove_all(d);
	fs::create_directories(d);
	return d;
}

void writeAgent(const fs::path& dir, const std::string& name,
                const std::string& role = "You are a helper.") {
	fs::create_directories(dir);
	std::ofstream(dir / "AGENT.md")
	    << "---\nname: " << name << "\ndescription: " << name
	    << " agent\nrole: " << role << "\n---\n" << name << " body.";
}

SubAgentDef registeredDef(const SubAgentRegistry& reg, const std::string& name) {
	for (const auto& d : reg.list()) {
		if (d.name == name) return d;
	}
	return SubAgentDef{};
}

}  // namespace

// ---------------------------------------------------------------------------
// SchemaExposesRunInBackground — the new field is visible in the JSON schema.
// ---------------------------------------------------------------------------
TEST(SubAgentToolAsync, SchemaExposesRunInBackground) {
	fs::path root = scratch();
	writeAgent(root / "foo", "foo");

	SubAgentRegistry reg;
	reg.discover(root, {});

	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	ASSERT_TRUE(tool.input_schema.contains("properties"));
	const auto& props = tool.input_schema["properties"];
	ASSERT_TRUE(props.contains("run_in_background"));
	const auto& rib = props["run_in_background"];
	EXPECT_EQ(rib.value("type", ""), "boolean");
	EXPECT_EQ(rib.value("default", true), false);

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// RunInBackgroundReturnsAsyncLaunched — async path returns a task_id JSON.
// ---------------------------------------------------------------------------
TEST(SubAgentToolAsync, RunInBackgroundReturnsAsyncLaunched) {
	fs::path root = scratch();
	writeAgent(root / "foo", "foo");

	SubAgentRegistry reg;
	reg.discover(root, {});

	// Slow LLM so the task is still Pending/Running when we look at the reply.
	auto release = std::make_shared<std::atomic<bool>>(false);
	FakeLlmClient fake([release](int) {
		while (!release->load()) std::this_thread::sleep_for(5ms);
		return textResp("eventual answer");
	});
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/2);

	Tool tool = makeSubAgentTool(reg, mgr);

	SubAgentDef def = registeredDef(reg, "foo");
	ASSERT_FALSE(def.name.empty());

	json input = {{"agent", "foo"},
	              {"task", "bar"},
	              {"run_in_background", true}};
	std::string output = tool.execute(input);

	// The reply must be a JSON envelope with async_launched + task_id.
	json reply = json::parse(output);
	EXPECT_EQ(reply.value("status", std::string{}), "async_launched");
	EXPECT_EQ(reply.value("agent", std::string{}), "foo");
	ASSERT_TRUE(reply.contains("task_id"));
	std::string taskId = reply["task_id"].get<std::string>();
	EXPECT_FALSE(taskId.empty());
	EXPECT_NE(taskId.find("subagent-"), std::string::npos);
	ASSERT_TRUE(reply.contains("hint"));
	EXPECT_NE(reply["hint"].get<std::string>().find("get_subagent_status"),
	          std::string::npos);

	// Sanity: the manager actually owns the task. The worker's mid-flight
	// (Pending or Running) so waitFor would still block; just confirm getInfo
	// knows the id.
	auto info = mgr.getInfo(taskId);
	ASSERT_TRUE(info.has_value());
	EXPECT_EQ(info->agent_name, "foo");
	EXPECT_EQ(info->task, "bar");
	EXPECT_TRUE(info->status == SubAgentTaskStatus::Running ||
	            info->status == SubAgentTaskStatus::Pending);

	// Release the fake so the worker can finish, then drain.
	release->store(true);
	SubAgentTaskInfo done = mgr.waitFor(taskId, 2000ms);
	EXPECT_EQ(done.status, SubAgentTaskStatus::Completed);
	EXPECT_EQ(done.output, "eventual answer");

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// RunInBackgroundDefaultsToFalse — omitted flag == sync path, returns text.
// ---------------------------------------------------------------------------
TEST(SubAgentToolAsync, RunInBackgroundDefaultsToFalse) {
	fs::path root = scratch();
	writeAgent(root / "foo", "foo");

	SubAgentRegistry reg;
	reg.discover(root, {});

	FakeLlmClient fake([](int) { return textResp("direct text reply"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	// Note: no run_in_background key.
	json input = {{"agent", "foo"}, {"task", "bar"}};
	std::string output = tool.execute(input);

	EXPECT_EQ(output, "direct text reply");
	// Exactly one LLM round-trip happened, inline.
	EXPECT_EQ(fake.calls_.load(), 1);
	// No task should have been registered with the manager on the sync path.
	EXPECT_TRUE(mgr.listTasks().empty());

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// RunInBackgroundExplicitFalseMatchesSync — explicit false == sync path.
// ---------------------------------------------------------------------------
TEST(SubAgentToolAsync, RunInBackgroundExplicitFalseMatchesSync) {
	fs::path root = scratch();
	writeAgent(root / "foo", "foo");

	SubAgentRegistry reg;
	reg.discover(root, {});

	FakeLlmClient fake([](int) { return textResp("sync body"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	json input = {{"agent", "foo"},
	              {"task", "bar"},
	              {"run_in_background", false}};
	std::string output = tool.execute(input);

	EXPECT_EQ(output, "sync body");
	EXPECT_TRUE(mgr.listTasks().empty());

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// AsyncLaunchedHasNoNestedLlmSideEffects — no LLM call before runAsync returns.
// ---------------------------------------------------------------------------
TEST(SubAgentToolAsync, AsyncLaunchedDoesNotBlockOnLlm) {
	fs::path root = scratch();
	writeAgent(root / "foo", "foo");

	SubAgentRegistry reg;
	reg.discover(root, {});

	// LLM sleeps 500ms — submission must return long before that elapses.
	FakeLlmClient fake([](int) {
		std::this_thread::sleep_for(500ms);
		return textResp("late");
	});
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	auto t0 = std::chrono::steady_clock::now();
	json input = {{"agent", "foo"},
	              {"task", "bar"},
	              {"run_in_background", true}};
	std::string output = tool.execute(input);
	auto elapsed = std::chrono::steady_clock::now() - t0;

	// Submission + JSON parse must be well under the LLM's sleep.
	EXPECT_LT(elapsed, 100ms);
	EXPECT_NE(output.find("async_launched"), std::string::npos);

	// Drain.
	auto reply = json::parse(output);
	std::string taskId = reply["task_id"].get<std::string>();
	SubAgentTaskInfo done = mgr.waitFor(taskId, 2000ms);
	EXPECT_EQ(done.status, SubAgentTaskStatus::Completed);

	fs::remove_all(root);
}
