#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "core/AgentLoop.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentRunner.h"

using namespace aicoder;

namespace {

// Re-usable fake LLM client — same pattern as AgentLoopTest.cpp.
class FakeLlmClient : public LlmClient {
public:
	explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}

	Response sendStream(const std::vector<Message>& msgs,
	                    const std::vector<ToolSpec>&,
	                    const DeltaCallback& onDelta) override {
		lastMessages_ = msgs;  // capture for inspection
		Response r = fn_(calls_++);
		std::string txt = assistantText(r.assistant_message);
		if (!txt.empty() && onDelta) onDelta(StreamDelta{txt, ""});
		return r;
	}

	int calls_ = 0;
	std::vector<Message> lastMessages_;  // last messages passed to sendStream

private:
	std::function<Response(int)> fn_;
};

Response textResp(std::string t) {
	return Response{Message{Role::Assistant, {TextBlock{std::move(t)}}}, "", "stop"};
}

Response toolResp(std::string id, std::string name, json input) {
	return Response{Message{Role::Assistant,
	                        {ToolUseBlock{std::move(id), std::move(name), std::move(input)}}},
	                "", "tool_calls"};
}

ToolRegistry registryWith(Tool t) {
	ToolRegistry r;
	r.registerTool(std::move(t));
	return r;
}

Tool echoTool() {
	Tool t;
	t.name = "echo";
	t.description = "echo";
	t.input_schema = json{{"type", "object"}};
	t.execute = [](const json& in) { return in.value("v", std::string("?")); };
	return t;
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

}  // namespace

// ---------------------------------------------------------------------------
// RunsSimpleSubAgent — no tools, single text response.
// ---------------------------------------------------------------------------
TEST(SubAgentRunner, RunsSimpleSubAgent) {
	FakeLlmClient fake([](int) { return textResp("hello from sub"); });
	ToolRegistry reg;
	SubAgentRunner runner(fake, reg);

	SubAgentDef def = makeDef("test_agent");
	SubAgentResult result = runner.run(def, "say hello");

	EXPECT_EQ(result.output, "hello from sub");
	EXPECT_FALSE(result.was_cancelled);
	EXPECT_EQ(fake.calls_, 1);
}

// ---------------------------------------------------------------------------
// CancelTokenPropagates — cancel before run, loop bails immediately.
// ---------------------------------------------------------------------------
TEST(SubAgentRunner, CancelTokenPropagates) {
	FakeLlmClient fake([](int) { return textResp("should not appear"); });
	ToolRegistry reg;
	SubAgentRunner runner(fake, reg);

	auto cancel = std::make_shared<std::atomic<bool>>(true);
	runner.setCancelToken(cancel);

	SubAgentDef def = makeDef("test_agent");
	SubAgentResult result = runner.run(def, "some task");

	EXPECT_TRUE(result.was_cancelled);
	EXPECT_NE(result.output.find("已取消"), std::string::npos);
	EXPECT_EQ(fake.calls_, 0);
}

// ---------------------------------------------------------------------------
// CancelDuringExecution — cancel set after first LLM call, caught next iter.
// ---------------------------------------------------------------------------
TEST(SubAgentRunner, CancelDuringExecution) {
	auto cancel = std::make_shared<std::atomic<bool>>(false);
	FakeLlmClient fake([&](int i) {
		if (i == 0) cancel->store(true);
		return i == 0 ? toolResp("c1", "echo", json{{"v", "ok"}}) : textResp("never");
	});
	ToolRegistry reg = registryWith(echoTool());
	SubAgentRunner runner(fake, reg);
	runner.setCancelToken(cancel);

	SubAgentDef def = makeDef("test_agent", {"echo"}, 10);
	SubAgentResult result = runner.run(def, "go");

	EXPECT_TRUE(result.was_cancelled);
	EXPECT_NE(result.output.find("已取消"), std::string::npos);
	EXPECT_LE(fake.calls_, 1);
}

// ---------------------------------------------------------------------------
// MaxIterationsEnforced — agent keeps calling tools, hits the cap.
// ---------------------------------------------------------------------------
TEST(SubAgentRunner, MaxIterationsEnforced) {
	FakeLlmClient fake(
	    [](int) { return toolResp("c", "echo", json{{"v", "x"}}); });
	ToolRegistry reg = registryWith(echoTool());

	SubAgentDef def = makeDef("looper", {"echo"}, /*maxIter=*/3);
	SubAgentRunner runner(fake, reg);

	SubAgentResult result = runner.run(def, "loop forever");

	EXPECT_EQ(fake.calls_, 3);
	EXPECT_NE(result.output.find("最大迭代"), std::string::npos);
}

// ---------------------------------------------------------------------------
// ContextAppendedToMessages — context string appears in the message list.
// ---------------------------------------------------------------------------
TEST(SubAgentRunner, ContextAppendedToMessages) {
	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry reg;
	SubAgentRunner runner(fake, reg);

	SubAgentDef def = makeDef("ctx_agent");
	runner.run(def, "do it", "file: src/main.cpp\n42 lines");

	// Scan the messages captured by the fake — context text should be present.
	bool foundContext = false;
	for (const auto& m : fake.lastMessages_) {
		for (const auto& b : m.content) {
			if (auto* tb = std::get_if<TextBlock>(&b)) {
				if (tb->text.find("src/main.cpp") != std::string::npos) {
					foundContext = true;
				}
			}
		}
	}
	EXPECT_TRUE(foundContext);
}
