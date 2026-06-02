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
