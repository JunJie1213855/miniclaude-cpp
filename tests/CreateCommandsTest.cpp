#include "commands/CreateCommands.h"
#include "workspace/Workspace.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;
using namespace aicoder;

namespace
{
  fs::path scratchRoot()
  {
    return fs::temp_directory_path() / "aicoder_create_test";
  }

  void resetScratch()
  {
    std::error_code ec;
    fs::remove_all(scratchRoot(), ec);
    fs::create_directories(scratchRoot(), ec);
    ASSERT_FALSE(ec) << "failed to create scratch dir";
  }

  class CreateCommandsTest : public ::testing::Test
  {
  protected:
    void SetUp() override { resetScratch(); }
    void TearDown() override
    {
      std::error_code ec;
      fs::remove_all(scratchRoot(), ec);
    }
  };
} // namespace

TEST_F(CreateCommandsTest, SkillWritesFrontmatterAndBody)
{
  fs::path skills = scratchRoot() / "skills";
  auto parsed = parseCreateArgs("greet 打招呼 | 你好！请用中文回复。");
  ASSERT_TRUE(parsed.has_value());
  auto out = createSkill(parsed->name, parsed->description, parsed->body, skills);
  EXPECT_TRUE(out.error.empty()) << out.error;
  EXPECT_EQ(out.name, "greet");
  EXPECT_EQ(out.description, "打招呼");
  EXPECT_EQ(out.body, "你好！请用中文回复。");
  EXPECT_EQ(out.path, skills / "greet" / "SKILL.md");

  auto content = readFile(out.path);
  ASSERT_TRUE(content.has_value());
  auto fm = parseFrontmatter(*content);
  EXPECT_EQ(fm.meta["name"], "greet");
  EXPECT_EQ(fm.meta["description"], "打招呼");
  EXPECT_EQ(fm.body, "你好！请用中文回复。\n");
}

TEST_F(CreateCommandsTest, SkillRejectsEmptyBody)
{
  fs::path skills = scratchRoot() / "skills";
  auto out = createSkill("greet", "打招呼", "", skills);
  EXPECT_FALSE(out.error.empty());
  EXPECT_FALSE(fs::exists(skills / "greet" / "SKILL.md"));
}

TEST_F(CreateCommandsTest, SkillRejectsMissingName)
{
  fs::path skills = scratchRoot() / "skills";
  auto out = createSkill("", "desc", "body content", skills);
  EXPECT_FALSE(out.error.empty());
}

TEST_F(CreateCommandsTest, SkillRejectsInvalidName)
{
  fs::path skills = scratchRoot() / "skills";
  auto out = createSkill("../etc/passwd", "desc", "body", skills);
  EXPECT_FALSE(out.error.empty());
  std::error_code ec;
  bool any = false;
  for (auto it = fs::recursive_directory_iterator(skills, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
  {
    if (it->is_regular_file(ec))
    {
      any = true;
      break;
    }
  }
  EXPECT_FALSE(any);
}

TEST_F(CreateCommandsTest, SkillRefusesOverwrite)
{
  fs::path skills = scratchRoot() / "skills";
  auto out1 = createSkill("greet", "hi", "hello body", skills);
  EXPECT_TRUE(out1.error.empty()) << out1.error;
  auto out2 = createSkill("greet", "hi", "different", skills);
  EXPECT_FALSE(out2.error.empty());
  EXPECT_NE(out2.error.find("文件已存在"), std::string::npos);
}

TEST_F(CreateCommandsTest, CommandWritesFileAndIsDiscoverable)
{
  fs::path cmds = scratchRoot() / "commands";
  auto out = createCommand("mycmd", "一个命令", "命令正文", cmds);
  EXPECT_TRUE(out.error.empty()) << out.error;
  EXPECT_EQ(out.path, cmds / "mycmd.md");

  auto found = listMarkdown(cmds);
  bool exists_in_list = false;
  for (const auto &p : found)
  {
    if (p == out.path)
    {
      exists_in_list = true;
      break;
    }
  }
  EXPECT_TRUE(exists_in_list);
}

TEST_F(CreateCommandsTest, AgentWritesFileAndIsDiscoverable)
{
  fs::path agents = scratchRoot() / "agents";
  auto out = createAgent("helper", "帮助代理", "代理正文", agents);
  EXPECT_TRUE(out.error.empty()) << out.error;
  EXPECT_EQ(out.path, agents / "helper" / "AGENT.md");

  auto found = listMarkdown(agents);
  bool exists_in_list = false;
  for (const auto &p : found)
  {
    if (p == out.path)
    {
      exists_in_list = true;
      break;
    }
  }
  EXPECT_TRUE(exists_in_list);
}

TEST_F(CreateCommandsTest, RuleWritesFile)
{
  fs::path rules = scratchRoot() / "rules";
  auto out = createRule("no_emoji", "", "text content", rules);
  EXPECT_TRUE(out.error.empty()) << out.error;
  EXPECT_EQ(out.path, rules / "no_emoji.md");
  EXPECT_TRUE(fs::exists(out.path));

  auto content = readFile(out.path);
  ASSERT_TRUE(content.has_value());
  auto fm = parseFrontmatter(*content);
  EXPECT_EQ(fm.meta["name"], "no_emoji");
  EXPECT_EQ(fm.meta.find("description"), fm.meta.end());
  EXPECT_EQ(fm.body, "text content\n");
}

TEST_F(CreateCommandsTest, ParserExtractsNameAndDescriptionWithoutBody)
{
  auto p = parseCreateArgs("greet 中文回复专家");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->name, "greet");
  EXPECT_EQ(p->description, "中文回复专家");
  EXPECT_EQ(p->body, "");
}

TEST_F(CreateCommandsTest, ParserExtractsNameOnly)
{
  auto p = parseCreateArgs("greet");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->name, "greet");
  EXPECT_EQ(p->description, "");
  EXPECT_EQ(p->body, "");
}

TEST_F(CreateCommandsTest, ParserRejectsEmptyInput)
{
  EXPECT_FALSE(parseCreateArgs("").has_value());
  EXPECT_FALSE(parseCreateArgs("   ").has_value());
  EXPECT_FALSE(parseCreateArgs("   | body").has_value());
}
