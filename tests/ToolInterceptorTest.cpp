#include "core/ToolInterceptor.h"
#include "core/Json.h"
#include <gtest/gtest.h>
#include <vector>
#include <string>

using namespace aicoder;

namespace
{
  ToolUseBlock mk(const std::string &id, const std::string &name, const json &input)
  {
    return ToolUseBlock{id, name, input};
  }
}

TEST(ToolInterceptor, NoPermissionToolsExecuteInOrder) {
  ToolInterceptor ic;
  std::vector<std::string> log;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"x", false}; });
  ic.setExecutor([&](const ToolUseBlock &tu) {
    log.push_back(tu.id);
    return ToolResultBlock{tu.id, "ok", false};
  });
  ic.setAsker([&](const ToolUseBlock &, const std::string &) {
    ADD_FAILURE() << "should not ask when needsPermission=false";
    return AskResult::Deny;
  });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Read", json::object()),
      mk("b", "Read", json::object()),
      mk("c", "Read", json::object()),
  };
  auto out = ic.intercept(queue);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_FALSE(out[1].is_error);
  EXPECT_FALSE(out[2].is_error);
  EXPECT_EQ(log, (std::vector<std::string>{"a", "b", "c"}));
}

TEST(ToolInterceptor, DenyAbortsQueueAndMarksRestAsSkipped) {
  ToolInterceptor ic;
  std::vector<std::string> log;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"x", true}; });
  // 第 2 个工具返回 is_error=true(失败),后续应 skipped
  ic.setExecutor([&](const ToolUseBlock &tu) {
    log.push_back(tu.id);
    if (tu.id == "b") return ToolResultBlock{tu.id, "boom", true};
    return ToolResultBlock{tu.id, "ok", false};
  });
  int askCount = 0;
  ic.setAsker([&](const ToolUseBlock &tu, const std::string &) {
    ++askCount;
    if (tu.id == "b") return AskResult::Deny;
    return AskResult::Allow;
  });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Bash", json::object()),
      mk("b", "Bash", json::object()),
      mk("c", "Bash", json::object()),
      mk("d", "Bash", json::object()),
  };
  auto out = ic.intercept(queue);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_FALSE(out[0].is_error) << out[0].content;
  EXPECT_TRUE(out[1].is_error) << out[1].content;
  EXPECT_EQ(out[1].content, "permission denied: Bash");
  EXPECT_TRUE(out[2].is_error);
  EXPECT_EQ(out[2].content, "skipped: previous tool was denied: Bash");
  EXPECT_TRUE(out[3].is_error);
  EXPECT_EQ(out[3].content, "skipped: previous tool was denied: Bash");
  // a 已问,b 已问,c/d 没问(被 skipped)
  EXPECT_EQ(askCount, 2);
  // 执行器只在 a 上跑了一次
  EXPECT_EQ(log, (std::vector<std::string>{"a"}));
}

TEST(ToolInterceptor, ExecutorFailureAbortsQueue) {
  ToolInterceptor ic;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"x", true}; });
  ic.setAsker([](const ToolUseBlock &, const std::string &) { return AskResult::Allow; });
  ic.setExecutor([&](const ToolUseBlock &tu) {
    if (tu.id == "b") return ToolResultBlock{tu.id, "fail", true};
    return ToolResultBlock{tu.id, "ok", false};
  });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Bash", json::object()),
      mk("b", "Bash", json::object()),
      mk("c", "Bash", json::object()),
  };
  auto out = ic.intercept(queue);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_TRUE(out[1].is_error);
  EXPECT_EQ(out[2].content, "skipped: previous tool was failed: Bash");
}

TEST(ToolInterceptor, AllowLookupSkipsAsking) {
  ToolInterceptor ic;
  std::vector<std::string> log;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Bash", true}; });
  ic.setAllowLookup([](const std::string &tool, const json &) { return tool == "Bash"; });
  ic.setExecutor([&](const ToolUseBlock &tu) {
    log.push_back(tu.id);
    return ToolResultBlock{tu.id, "ok", false};
  });
  int askCount = 0;
  ic.setAsker([&](const ToolUseBlock &, const std::string &) {
    ++askCount;
    return AskResult::Allow;
  });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Bash", json::object()),
      mk("b", "Bash", json::object()),
  };
  auto out = ic.intercept(queue);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_FALSE(out[1].is_error);
  EXPECT_EQ(askCount, 0);  // 命中 lookup 就不问
  EXPECT_EQ(log, (std::vector<std::string>{"a", "b"}));
}

TEST(ToolInterceptor, AllowForeverInvokesHook) {
  ToolInterceptor ic;
  std::vector<std::pair<std::string, json>> foreverLog;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Bash", true}; });
  ic.setAsker([](const ToolUseBlock &, const std::string &) { return AskResult::AllowForever; });
  ic.setAllowForeverHook([&](const std::string &tool, const json &input) {
    foreverLog.push_back({tool, input});
  });
  ic.setExecutor([&](const ToolUseBlock &tu) { return ToolResultBlock{tu.id, "ok", false}; });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Bash", json{{"command", "cmake -S . -B build"}}),
  };
  auto out = ic.intercept(queue);
  EXPECT_FALSE(out[0].is_error);
  ASSERT_EQ(foreverLog.size(), 1u);
  EXPECT_EQ(foreverLog[0].first, "Bash");
  EXPECT_EQ(foreverLog[0].second["command"], "cmake -S . -B build");
}
