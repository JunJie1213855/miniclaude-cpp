#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "subagent/SubAgentRegistry.h"

using namespace aicoder;
namespace fs = std::filesystem;

namespace {

fs::path scratch() {
	fs::path d = fs::temp_directory_path() / "aicoder_subagent_test";
	fs::remove_all(d);
	fs::create_directories(d);
	return d;
}

void writeAgent(const fs::path& dir, const std::string& name,
                const std::string& desc, const std::string& body,
                const std::string& tools = "") {
	fs::create_directories(dir);
	std::string toolsLine = tools.empty() ? "" : "tools: " + tools + "\n";
	std::ofstream(dir / "AGENT.md")
	    << "---\nname: " << name << "\ndescription: " << desc << "\n"
	    << toolsLine << "---\n" << body;
}

} // namespace

TEST(SubAgentRegistry, DiscoverParsesValidAgentDef) {
	fs::path root = scratch();
	fs::path d = root / "agents";
	writeAgent(d, "test-agent", "A test agent", "BODY TEXT",
	           R"(["read_file","grep"])");
	SubAgentRegistry reg;
	reg.discover(d, {});
	const auto* def = reg.find("test-agent");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->name, "test-agent");
	EXPECT_EQ(def->description, "A test agent");
	EXPECT_EQ(def->body, "BODY TEXT");
	ASSERT_EQ(def->tools.size(), 2u);
	EXPECT_EQ(def->tools[0], "read_file");
	EXPECT_EQ(def->tools[1], "grep");
	EXPECT_EQ(reg.find("missing"), nullptr);
	fs::remove_all(root);
}

TEST(SubAgentRegistry, ProjectOverridesGlobal) {
	fs::path root = scratch();
	fs::path g = root / "global";
	fs::path p = root / "project";
	writeAgent(g, "helper", "global helper", "GLOBAL BODY");
	writeAgent(p, "helper", "project helper", "PROJECT BODY");

	SubAgentRegistry reg;
	reg.discover(g, p);
	const auto* def = reg.find("helper");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->description, "project helper");
	EXPECT_EQ(def->body, "PROJECT BODY");
	fs::remove_all(root);
}

TEST(SubAgentRegistry, EmptyDirectoryGraceful) {
	fs::path root = scratch();
	SubAgentRegistry reg;
	EXPECT_NO_THROW(reg.discover(root, {}));
	EXPECT_TRUE(reg.empty());

	// discover on nonexistent dir does not throw
	EXPECT_NO_THROW(reg.discover("/nonexistent/path/12345", {}));
	EXPECT_TRUE(reg.empty());
	fs::remove_all(root);
}

TEST(SubAgentRegistry, ListReturnsAll) {
	fs::path root = scratch();
	writeAgent(root / "a", "alpha", "First agent", "A");
	writeAgent(root / "b", "beta", "Second agent", "B");
	SubAgentRegistry reg;
	reg.discover(root, {});
	auto agents = reg.list();
	EXPECT_EQ(agents.size(), 2u);
	fs::remove_all(root);
}

TEST(SubAgentRegistry, PromptListFormat) {
	fs::path root = scratch();
	writeAgent(root, "test", "does testing", "BODY");
	SubAgentRegistry reg;
	reg.discover(root, {});
	std::string prompt = reg.promptList();
	EXPECT_NE(prompt.find("- test: does testing"), std::string::npos);
	fs::remove_all(root);
}
