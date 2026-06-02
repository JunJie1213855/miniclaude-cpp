#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "commands/CommandRouter.h"
#include "commands/CommandRegistry.h"
#include "skills/SkillRegistry.h"

using namespace aicoder;

TEST(CommandRouter, QuitReturnsQuit) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/quit", msgs).result, CommandResult::Quit);
}

TEST(CommandRouter, ClearKeepsOnlySystemMessages) {
  CommandRouter r;
  std::vector<Message> msgs = {systemText("sys"), userText("a"),
                               Message{Role::Assistant, {TextBlock{"b"}}}};
  EXPECT_EQ(r.handle("/clear", msgs).result, CommandResult::Cleared);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0].role, Role::System);
}

TEST(CommandRouter, NormalTextIsNotACommand) {
  CommandRouter r;
  std::vector<Message> msgs = {systemText("sys")};
  EXPECT_EQ(r.handle("hello", msgs).result, CommandResult::NotACommand);
  EXPECT_EQ(msgs.size(), 1u);   // 未改动
}

TEST(CommandRouter, ExitIsAliasOfQuit) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/exit", msgs).result, CommandResult::Quit);
}

TEST(CommandRouter, SessionsReturnsSessions) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/sessions", msgs).result, CommandResult::Sessions);
  EXPECT_TRUE(msgs.empty());  // 不修改 messages
}

TEST(CommandRouter, TolerantToSurroundingWhitespace) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/quit ", msgs).result, CommandResult::Quit);    // 尾随空格
  EXPECT_EQ(r.handle("  /quit", msgs).result, CommandResult::Quit);   // 前导空格
  EXPECT_EQ(r.handle("/quit\r", msgs).result, CommandResult::Quit);   // 行尾 CR
  std::vector<Message> msgs2 = {systemText("sys"), userText("a")};
  EXPECT_EQ(r.handle("/clear\n", msgs2).result, CommandResult::Cleared);
}

TEST(CommandRouter, TemplateCommandExpandsArguments) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_tmpl";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  { std::ofstream(d / "g" / "review.md") << "Please review: $ARGUMENTS"; }
  CommandRegistry reg; reg.discover(d / "g", d / "p");
  CommandRouter r(reg);
  std::vector<Message> msgs;
  auto out = r.handle("/review src/foo.cpp", msgs);
  EXPECT_EQ(out.result, CommandResult::Prompt);
  EXPECT_EQ(out.prompt, "Please review: src/foo.cpp");
  fs::remove_all(d);
}

TEST(CommandRouter, UnknownSlashIsNotACommand) {
  CommandRouter r; std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/nope", msgs).result, CommandResult::NotACommand);
}

TEST(CommandRouter, ListsBuiltinsPlusTemplates) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_list";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  { std::ofstream(d / "g" / "review.md") << "x"; }
  CommandRegistry reg; reg.discover(d / "g", d / "p");
  CommandRouter r(reg);
  bool hasQuit=false, hasReview=false;
  for (const auto& c : r.commands()) { if (c.name=="/quit") hasQuit=true; if (c.name=="/review") hasReview=true; }
  EXPECT_TRUE(hasQuit); EXPECT_TRUE(hasReview);
  fs::remove_all(d);
}

namespace {
void writeSkillFile(const std::filesystem::path& base, const std::string& name,
                    const std::string& desc, const std::string& body) {
  std::filesystem::create_directories(base / name);
  std::ofstream(base / name / "SKILL.md")
      << "---\nname: " << name << "\ndescription: " << desc << "\n---\n" << body;
}
}  // namespace

TEST(CommandRouter, SkillSlashExpandsBodyWithArguments) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_skill";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  writeSkillFile(d / "g", "fmt", "format code", "Run formatter on: $ARGUMENTS");
  SkillRegistry sreg; sreg.discover(d / "g", d / "p");
  CommandRouter r({}, &sreg);
  std::vector<Message> msgs;
  auto out = r.handle("/fmt src/foo.cpp", msgs);
  EXPECT_EQ(out.result, CommandResult::Prompt);
  EXPECT_EQ(out.prompt, "Run formatter on: src/foo.cpp");
  fs::remove_all(d);
}

TEST(CommandRouter, ReloadSkillsRediscoversFromDirs) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_reload";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  SkillRegistry sreg;
  CommandRouter r({}, &sreg, d / "g", d / "p");
  std::vector<Message> msgs;
  // 初次：目录空 → reload 后仍 0
  auto out1 = r.handle("/reload-skills", msgs);
  EXPECT_EQ(out1.result, CommandResult::Reloaded);
  EXPECT_EQ(sreg.list().size(), 0u);
  EXPECT_NE(out1.prompt.find("0"), std::string::npos);
  // 运行时新增 SKILL.md → reload 后能发现
  writeSkillFile(d / "g", "new_skill", "newly added", "body");
  auto out2 = r.handle("/reload-skills", msgs);
  EXPECT_EQ(out2.result, CommandResult::Reloaded);
  EXPECT_EQ(sreg.list().size(), 1u);
  EXPECT_NE(out2.prompt.find("1"), std::string::npos);
  // /reload-skills 也在 commands() 列表里
  bool hasReload = false;
  for (const auto &c : r.commands()) if (c.name == "/reload-skills") hasReload = true;
  EXPECT_TRUE(hasReload);
  fs::remove_all(d);
}

TEST(CommandRouter, SkillWithoutArgumentsPlaceholderGetsPrependedUserRequest) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_skill_prepend";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  writeSkillFile(d / "g", "doit", "do something", "step 1\nstep 2");  // 正文无 $ARGUMENTS
  SkillRegistry sreg; sreg.discover(d / "g", d / "p");
  CommandRouter r({}, &sreg);
  std::vector<Message> msgs;
  auto out = r.handle("/doit make it work", msgs);
  EXPECT_EQ(out.result, CommandResult::Prompt);
  EXPECT_EQ(out.prompt, "用户请求: make it work\n\nstep 1\nstep 2");
  auto bare = r.handle("/doit", msgs);  // 无 args 时不前置
  EXPECT_EQ(bare.prompt, "step 1\nstep 2");
  fs::remove_all(d);
}

TEST(CommandRouter, SkillAppearsInCommandsList) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_skill_list";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  writeSkillFile(d / "g", "explain", "explain code", "body");
  SkillRegistry sreg; sreg.discover(d / "g", d / "p");
  CommandRouter r({}, &sreg);
  bool hasExplain = false, hasQuit = false;
  for (const auto& c : r.commands()) {
    if (c.name == "/explain") hasExplain = true;
    if (c.name == "/quit") hasQuit = true;
  }
  EXPECT_TRUE(hasExplain);
  EXPECT_TRUE(hasQuit);
  fs::remove_all(d);
}

TEST(CommandRouter, TemplateCommandWinsOverSameNameSkill) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_priority";
  fs::remove_all(d);
  fs::create_directories(d / "cmd_g"); fs::create_directories(d / "cmd_p");
  fs::create_directories(d / "skill_g"); fs::create_directories(d / "skill_p");
  { std::ofstream(d / "cmd_g" / "foo.md") << "CMD BODY"; }
  writeSkillFile(d / "skill_g", "foo", "skill foo", "SKILL BODY");
  CommandRegistry creg; creg.discover(d / "cmd_g", d / "cmd_p");
  SkillRegistry sreg; sreg.discover(d / "skill_g", d / "skill_p");
  CommandRouter r(std::move(creg), &sreg);
  std::vector<Message> msgs;
  auto out = r.handle("/foo", msgs);
  EXPECT_EQ(out.result, CommandResult::Prompt);
  EXPECT_EQ(out.prompt, "CMD BODY");  // command wins
  fs::remove_all(d);
}
