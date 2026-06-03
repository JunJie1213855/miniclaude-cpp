#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "core/Errors.h"
#include "core/Tool.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentTool.h"

using namespace aicoder;
namespace fs = std::filesystem;

namespace {

// Minimal FakeLlmClient — same pattern as AgentLoopTest.cpp.
class FakeLlmClient : public LlmClient {
public:
	explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}

	Response sendStream(const std::vector<Message>&,
	                    const std::vector<ToolSpec>&,
	                    const DeltaCallback& onDelta) override {
		Response r = fn_(calls_++);
		std::string txt = assistantText(r.assistant_message);
		if (!txt.empty() && onDelta) onDelta(StreamDelta{txt, ""});
		return r;
	}

	int calls_ = 0;

private:
	std::function<Response(int)> fn_;
};

Response textResp(std::string t) {
	return Response{Message{Role::Assistant, {TextBlock{std::move(t)}}}, "", "stop"};
}

fs::path scratch() {
	fs::path d = fs::temp_directory_path() / "aicoder_subagenttool_test";
	fs::remove_all(d);
	fs::create_directories(d);
	return d;
}

void writeAgent(const fs::path& dir, const std::string& name,
                const std::string& desc, const std::string& role,
                const std::string& body, const std::string& tools = "") {
	fs::create_directories(dir);
	std::string toolsLine = tools.empty() ? "" : "tools: " + tools + "\n";
	std::ofstream(dir / "AGENT.md")
	    << "---\nname: " << name << "\ndescription: " << desc << "\n"
	    << "role: " << role << "\n"
	    << toolsLine << "---\n" << body;
}

}  // namespace

// ---------------------------------------------------------------------------
// ToolSchemaHasAgentEnum — agent property lists registered agent names.
// ---------------------------------------------------------------------------
TEST(SubAgentTool, ToolSchemaHasAgentEnum) {
	fs::path root = scratch();
	writeAgent(root / "alice", "alice", "Alice agent", "You are Alice.",
	           "Alice body.");
	writeAgent(root / "bob", "bob", "Bob agent", "You are Bob.",
	           "Bob body.", R"(["echo"])");

	SubAgentRegistry reg;
	reg.discover(root, {});

	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	// Check that the input schema has the "agent" property with an enum.
	ASSERT_TRUE(tool.input_schema.contains("properties"));
	const auto& props = tool.input_schema["properties"];
	ASSERT_TRUE(props.contains("agent"));
	const auto& agentProp = props["agent"];
	EXPECT_EQ(agentProp.value("type", ""), "string");
	ASSERT_TRUE(agentProp.contains("enum"));
	const auto& enumVals = agentProp["enum"];
	ASSERT_TRUE(enumVals.is_array());
	EXPECT_GE(enumVals.size(), 2u);

	// Verify expected names are present.
	bool hasAlice = false, hasBob = false;
	for (const auto& v : enumVals) {
		if (v.get<std::string>() == "alice") hasAlice = true;
		if (v.get<std::string>() == "bob") hasBob = true;
	}
	EXPECT_TRUE(hasAlice);
	EXPECT_TRUE(hasBob);

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// ToolSchemaHasTaskField — task is required in the schema.
// ---------------------------------------------------------------------------
TEST(SubAgentTool, ToolSchemaHasTaskField) {
	SubAgentRegistry reg;  // empty is fine — schema structure is static
	FakeLlmClient fake([](int) { return textResp("ok"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);
	Tool tool = makeSubAgentTool(reg, mgr);

	ASSERT_TRUE(tool.input_schema.contains("properties"));
	const auto& props = tool.input_schema["properties"];
	ASSERT_TRUE(props.contains("task"));
	EXPECT_EQ(props["task"].value("type", ""), "string");

	// "task" must be in the required array.
	ASSERT_TRUE(tool.input_schema.contains("required"));
	const auto& req = tool.input_schema["required"];
	bool taskRequired = false;
	for (const auto& r : req) {
		if (r.get<std::string>() == "task") taskRequired = true;
	}
	EXPECT_TRUE(taskRequired);
}

// ---------------------------------------------------------------------------
// ExecuteWithValidAgentSucceeds — known agent name produces runner output.
// ---------------------------------------------------------------------------
TEST(SubAgentTool, ExecuteWithValidAgentSucceeds) {
	fs::path root = scratch();
	writeAgent(root / "helper", "helper", "Helper agent",
	           "You are a helpful assistant.", "Helper body.");

	SubAgentRegistry reg;
	reg.discover(root, {});

	FakeLlmClient fake([](int) { return textResp("task completed successfully"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	json input = {{"agent", "helper"}, {"task", "summarize this"}};
	std::string output = tool.execute(input);

	EXPECT_EQ(output, "task completed successfully");
	EXPECT_EQ(fake.calls_, 1);

	fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// ExecuteWithUnknownAgentThrows — unknown agent name throws ToolError.
// ---------------------------------------------------------------------------
TEST(SubAgentTool, ExecuteWithUnknownAgentThrows) {
	SubAgentRegistry reg;  // empty registry
	FakeLlmClient fake([](int) { return textResp("never"); });
	ToolRegistry toolReg;
	SubAgentManager mgr(fake, toolReg, reg, {}, /*bgThreads=*/1);

	Tool tool = makeSubAgentTool(reg, mgr);

	json input = {{"agent", "nonexistent"}, {"task", "do something"}};
	EXPECT_THROW({ tool.execute(input); }, ToolError);
}
