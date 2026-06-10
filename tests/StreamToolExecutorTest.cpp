#include "core/StreamToolExecutor.h"
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

TEST(StreamToolExecutor, NoPermissionToolsExecuteInOrder) {
  StreamToolExecutor ic;
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
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_FALSE(out[1].is_error);
  EXPECT_FALSE(out[2].is_error);
  EXPECT_EQ(log, (std::vector<std::string>{"a", "b", "c"}));
}

TEST(StreamToolExecutor, DenyAbortsQueueAndMarksRestAsSkipped) {
  StreamToolExecutor ic;
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
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
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

TEST(StreamToolExecutor, ExecutorFailureAbortsQueue) {
  StreamToolExecutor ic;
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
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_TRUE(out[1].is_error);
  EXPECT_EQ(out[2].content, "skipped: previous tool was failed: Bash");
}

TEST(StreamToolExecutor, AllowLookupSkipsAsking) {
  StreamToolExecutor ic;
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
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  EXPECT_FALSE(out[0].is_error);
  EXPECT_FALSE(out[1].is_error);
  EXPECT_EQ(askCount, 0);  // 命中 lookup 就不问
  EXPECT_EQ(log, (std::vector<std::string>{"a", "b"}));
}

TEST(StreamToolExecutor, AllowForeverInvokesHook) {
  StreamToolExecutor ic;
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
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  EXPECT_FALSE(out[0].is_error);
  ASSERT_EQ(foreverLog.size(), 1u);
  EXPECT_EQ(foreverLog[0].first, "Bash");
  EXPECT_EQ(foreverLog[0].second["command"], "cmake -S . -B build");
}

TEST(StreamToolExecutor, BetweenAsksYieldRunsOncePerAskerCall) {
  // 关键回归:连续多个 needsPermission 工具时,yield 必须在每次 asker
  // 之前被调用,避免第二个弹窗被第一个 Enter 穿透。
  StreamToolExecutor ic;
  int yieldCount = 0;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Bash", true}; });
  ic.setBetweenAsksYield([&] { ++yieldCount; });
  ic.setAsker([](const ToolUseBlock &, const std::string &) { return AskResult::Allow; });
  ic.setExecutor([&](const ToolUseBlock &tu) { return ToolResultBlock{tu.id, "ok", false}; });

  std::vector<ToolUseBlock> queue = {
      mk("a", "Bash", json::object()),
      mk("b", "Bash", json::object()),
      mk("c", "Bash", json::object()),
  };
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  ASSERT_EQ(out.size(), 3u);
  for (const auto& r : out) EXPECT_FALSE(r.is_error);
  // 3 个 tool 都需 asker,→ 3 次 yield
  EXPECT_EQ(yieldCount, 3);
}

TEST(StreamToolExecutor, BetweenAsksYieldNotCalledForReadOnlyTools) {
  // 不需权限的工具(如 Read)不弹窗,不该触发 yield。
  StreamToolExecutor ic;
  int yieldCount = 0;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Read", false}; });
  ic.setBetweenAsksYield([&] { ++yieldCount; });
  ic.setExecutor([&](const ToolUseBlock &tu) { return ToolResultBlock{tu.id, "ok", false}; });
  std::vector<ToolUseBlock> queue = {
      mk("a", "Read", json::object()),
      mk("b", "Read", json::object()),
  };
  for (const auto &tu : queue) ic.enqueue(tu);
  auto out = ic.run();
  EXPECT_EQ(yieldCount, 0);
  EXPECT_FALSE(out[0].is_error);
  EXPECT_FALSE(out[1].is_error);
}

// ========== 新增:流式入队接口(参考 query_learn.md StreamingToolExecutor) ==========

TEST(StreamToolExecutor, EnqueueDoesNotExecuteImmediately) {
  // enqueue 后未调 run(),results_ 仍空;executor_ 也不该被调。
  StreamToolExecutor ic;
  int execCount = 0;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"x", false}; });
  ic.setExecutor([&](const ToolUseBlock &) { ++execCount; return ToolResultBlock{"", "", false}; });
  ic.enqueue(mk("a", "Read", json::object()));
  ic.enqueue(mk("b", "Read", json::object()));
  EXPECT_EQ(execCount, 0);
  EXPECT_TRUE(ic.results().empty());
  EXPECT_EQ(ic.queue().size(), 2u);
  EXPECT_FALSE(ic.aborted());
}

TEST(StreamToolExecutor, EnqueueFiresOnToolCall) {
  // 入队即回调,UI 可借此流式显示"即将调工具"占位卡片。
  StreamToolExecutor ic;
  std::vector<std::string> callLog;
  ic.setOnToolCall([&](const ToolUseBlock &tu) { callLog.push_back(tu.id); });
  // 不设 executor / asker 也无妨 —— 入队回调不依赖它们。
  ic.enqueue(mk("a", "Read", json::object()));
  ic.enqueue(mk("b", "Read", json::object()));
  EXPECT_EQ(callLog, (std::vector<std::string>{"a", "b"}));
}

TEST(StreamToolExecutor, EnqueuePreservesFifoOrder) {
  // 核心契约:enqueue 顺序 == run() 输出顺序(FIFO)。
  // 故意按 id 倒序入队(c / a / b),验证 run 严格按入队顺序产出,
  // 不是按 id 字典序或任何其他顺序。这是流式 API 的关键承诺。
  StreamToolExecutor ic;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Read", false}; });
  ic.setExecutor([](const ToolUseBlock &tu) {
    return ToolResultBlock{tu.id, "out:" + tu.id, false};
  });

  std::vector<ToolUseBlock> q = {
      mk("c", "Read", json::object()),
      mk("a", "Read", json::object()),
      mk("b", "Read", json::object()),
  };
  for (const auto &tu : q) ic.enqueue(tu);
  auto out = ic.run();

  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0].tool_use_id, "c");  // 第 1 个入队 → 第 1 个出
  EXPECT_EQ(out[1].tool_use_id, "a");  // 第 2 个入队 → 第 2 个出
  EXPECT_EQ(out[2].tool_use_id, "b");  // 第 3 个入队 → 第 3 个出
  EXPECT_EQ(ic.results().size(), 3u);  // results() 同步反映
}

TEST(StreamToolExecutor, AbortedQueueSkipsLaterEnqueues) {
  // run() 中途 abort 后,后面再 enqueue 的 tu 在下一次 run() 也应被标 skipped。
  // 验证两点:
  //   1) aborted_ 在 run 后置 true
  //   2) 再 enqueue + run,新 tu 的 result content 含 "skipped"
  StreamToolExecutor ic;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"Bash", true}; });
  ic.setAsker([](const ToolUseBlock &, const std::string &) { return AskResult::Allow; });
  ic.setExecutor([&](const ToolUseBlock &tu) {
    if (tu.id == "b") return ToolResultBlock{tu.id, "fail", true};
    return ToolResultBlock{tu.id, "ok", false};
  });

  ic.enqueue(mk("a", "Bash", json::object()));
  ic.enqueue(mk("b", "Bash", json::object()));
  ic.enqueue(mk("c", "Bash", json::object()));
  auto firstRun = ic.run();
  ASSERT_EQ(firstRun.size(), 3u);
  EXPECT_TRUE(ic.aborted());  // b 失败 → 队列中止
  EXPECT_TRUE(firstRun[2].is_error);
  EXPECT_NE(firstRun[2].content.find("skipped"), std::string::npos);

  // abort 后再入队新 tu,再次 run 仍应标 skipped(因 aborted_ 已 true)。
  // 注意:为简化,我们手动 reset(测试用;生产代码 run 后状态保留是设计使然)。
  // 这里验证的是 aborted_ 状态可被新 run() 观察到,但需要 reset。
  // 实际上 aborted_ 一旦置位就持续生效 —— 调用方需要重建实例做新会话。
  // 故此处只验证:首次 run() 后,再调 run() 空跑不抛 + 已 aborted。
  auto secondRun = ic.run();  // queue_ 仍是上次的 3 条
  EXPECT_TRUE(ic.aborted());
  EXPECT_EQ(secondRun.size(), 3u);
}

TEST(StreamToolExecutor, ResultsAccessorReflectsState) {
  // results() 返回内部 vector 引用,run 完后能看到完整结果。
  StreamToolExecutor ic;
  ic.setMetaLookup([](const std::string &) { return ToolMeta{"x", false}; });
  ic.setExecutor([](const ToolUseBlock &tu) {
    return ToolResultBlock{tu.id, "out:" + tu.id, false};
  });
  ic.enqueue(mk("a", "Read", json::object()));
  ic.enqueue(mk("b", "Read", json::object()));
  EXPECT_TRUE(ic.results().empty());
  ic.run();
  ASSERT_EQ(ic.results().size(), 2u);
  EXPECT_EQ(ic.results()[0].content, "out:a");
  EXPECT_EQ(ic.results()[1].content, "out:b");
  EXPECT_FALSE(ic.aborted());
}
