#include <gtest/gtest.h>
#include "sessions/CliArgs.h"
using namespace aicoder;

TEST(CliArgs, NoArgsIsNewSession) {
  const char* argv[] = {"aicoder_tui"};
  auto a = parseCliArgs(1, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::NewSession);
}

TEST(CliArgs, DashCIsContinueLatest) {
  const char* argv[] = {"aicoder_tui", "-c"};
  auto a = parseCliArgs(2, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ContinueLatest);
}

TEST(CliArgs, DashDashResumeIsPicker) {
  const char* argv[] = {"aicoder_tui", "--resume"};
  auto a = parseCliArgs(2, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ResumePicker);
}

TEST(CliArgs, ResumeWinsWhenBoth) {
  const char* argv[] = {"aicoder_tui", "-c", "--resume"};
  auto a = parseCliArgs(3, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ResumePicker);
}

TEST(CliArgs, UnknownArgReturnsNullopt) {
  const char* argv[] = {"aicoder_tui", "--what"};
  EXPECT_FALSE(parseCliArgs(2, argv).has_value());
}

TEST(CliArgs, ListSessionsFlag) {
  const char* argv[] = {"aicoder_tui", "--list-sessions"};
  auto a = parseCliArgs(2, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ListSessions);
}

TEST(CliArgs, ListSessionsWinsOverOthers) {
  const char* argv[] = {"aicoder_tui", "-c", "--resume", "--list-sessions"};
  auto a = parseCliArgs(4, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ListSessions);
}

TEST(CliArgs, SystemPromptFlag) {
  const char* argv[] = {"aicoder_tui", "--system-prompt", "You are X"};
  auto a = parseCliArgs(3, argv);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(a->system_prompt.has_value());
  EXPECT_EQ(*a->system_prompt, "You are X");
  EXPECT_FALSE(a->user_prompt.has_value());
}

TEST(CliArgs, UserFlag) {
  const char* argv[] = {"aicoder_tui", "--user", "hi"};
  auto a = parseCliArgs(3, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_FALSE(a->system_prompt.has_value());
  ASSERT_TRUE(a->user_prompt.has_value());
  EXPECT_EQ(*a->user_prompt, "hi");
}

TEST(CliArgs, BothFlagsParseCleanly) {
  const char* argv[] = {"aicoder_tui",
                        "--system-prompt", "S",
                        "--user", "U"};
  auto a = parseCliArgs(5, argv);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(a->system_prompt.has_value());
  ASSERT_TRUE(a->user_prompt.has_value());
  EXPECT_EQ(*a->system_prompt, "S");
  EXPECT_EQ(*a->user_prompt, "U");
  // 同时给两个 flag 不应改变 CliMode 优先级 —— 仍然默认为 NewSession。
  // main.cpp 检测到 hasSys==hasUser 时才走 one-shot 路径。
  EXPECT_EQ(a->mode, CliMode::NewSession);
}

TEST(CliArgs, ShortForms) {
  const char* argv[] = {"aicoder_tui", "-s", "S", "-u", "U"};
  auto a = parseCliArgs(5, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(*a->system_prompt, "S");
  EXPECT_EQ(*a->user_prompt, "U");
}
