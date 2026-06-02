#include <gtest/gtest.h>
#include <functional>
#include "core/AgentLoop.h"
#include "core/ToolRegistry.h"
#include "core/Errors.h"
#include "llm/LlmClient.h"

using namespace aicoder;

namespace {
// 由测试脚本控制每次调用返回什么（按调用序号）；可选地吐出增量。
class FakeLlmClient : public LlmClient {
public:
  explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}
  Response sendStream(const std::vector<Message>&, const std::vector<ToolSpec>&,
                      const DeltaCallback& onDelta) override {
    Response r = fn_(calls_++);
    // 把本轮文本作为单个增量吐出，便于测试流式转发
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
  Tool t; t.name = "echo"; t.description = "echo";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json& in) { return in.value("v", std::string("?")); };
  return t;
}
Tool dangerTool() {  // needsPermission=true：用于权限门控测试
  Tool t = echoTool();
  t.name = "danger";
  t.needsPermission = true;
  return t;
}
}

TEST(AgentLoop, TextOnlyStopsAfterOneCall) {
  FakeLlmClient fake([](int) { return textResp("done"); });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("hi")};
  std::string reply = loop.run(msgs);
  EXPECT_EQ(reply, "done");
  EXPECT_EQ(fake.calls_, 1);
  EXPECT_EQ(msgs.back().role, Role::Assistant);
}

TEST(AgentLoop, ToolUseThenTextFeedsResultBack) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "RESULT"}}) : textResp("final");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("use tool")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(reply, "final");
  EXPECT_EQ(fake.calls_, 2);
  bool found = false;
  for (const auto& m : msgs)
    if (m.role == Role::Tool)
      for (const auto& b : m.content)
        if (auto* tr = std::get_if<ToolResultBlock>(&b)) {
          EXPECT_EQ(tr->content, "RESULT");
          EXPECT_FALSE(tr->is_error);
          found = true;
        }
  EXPECT_TRUE(found);
}

TEST(AgentLoop, ToolErrorIsFedBackAndLoopContinues) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "ghost", json::object()) : textResp("recovered");
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("x")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(reply, "recovered");
  bool sawError = false;
  for (const auto& m : msgs)
    if (m.role == Role::Tool)
      for (const auto& b : m.content)
        if (auto* tr = std::get_if<ToolResultBlock>(&b))
          if (tr->is_error) sawError = true;
  EXPECT_TRUE(sawError);
}

TEST(AgentLoop, HitsMaxIterationsGracefully) {
  FakeLlmClient fake([](int) { return toolResp("c", "echo", json{{"v", "x"}}); });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 3);
  std::vector<Message> msgs = {userText("loop forever")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(fake.calls_, 3);
  EXPECT_NE(reply.find("最大迭代"), std::string::npos);
}

TEST(AgentLoop, ForwardsStreamDeltaToCallback) {
  FakeLlmClient fake([](int) { return textResp("hello"); });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("hi")};
  std::string streamed;
  loop.run(msgs, [&](const StreamDelta& d) { streamed += d.text; });
  EXPECT_EQ(streamed, "hello");
}

// ---- 权限门控（permission gating）----

TEST(AgentLoop, ReadOnlyToolDoesNotAskPermission) {
  // 只读工具（needsPermission=false）不应触发权限回调 —— 直接执行。
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "R"}}) : textResp("done");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("go")};
  int asked = 0;
  std::string reply = loop.run(msgs, {}, [&](const std::string&, const json&) {
    ++asked;
    return true;
  });
  EXPECT_EQ(reply, "done");
  EXPECT_EQ(asked, 0);  // 关键：只读工具不询问
}

TEST(AgentLoop, NeedsPermissionToolAsksWithRealToolName) {
  // needsPermission 工具触发权限回调，且回调收到真实工具名（不是硬编码 write_file）。
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "danger", json{{"v", "R"}}) : textResp("done");
  });
  ToolRegistry reg = registryWith(dangerTool());
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("go")};
  std::string askedTool;
  std::string reply = loop.run(msgs, {}, [&](const std::string& name, const json&) {
    askedTool = name;
    return true;
  });
  EXPECT_EQ(reply, "done");
  EXPECT_EQ(askedTool, "danger");
}

TEST(AgentLoop, PermissionDeniedSkipsExecution) {
  // 拒绝权限 → 工具不执行，结果是 permission denied 错误，循环继续。
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "danger", json{{"v", "R"}}) : textResp("done");
  });
  ToolRegistry reg = registryWith(dangerTool());
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("go")};
  std::string reply =
      loop.run(msgs, {}, [](const std::string&, const json&) { return false; });
  EXPECT_EQ(reply, "done");
  bool denied = false;
  for (const auto& m : msgs)
    if (m.role == Role::Tool)
      for (const auto& b : m.content)
        if (auto* tr = std::get_if<ToolResultBlock>(&b))
          if (tr->content.find("permission denied") != std::string::npos) denied = true;
  EXPECT_TRUE(denied);
}

// ---- 工具调用上报（onToolCall）----

TEST(AgentLoop, OnToolCallReportsInvocation) {
  // 每次工具执行后都上报：工具名、参数、结果内容、isError=false。
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "RESULT"}}) : textResp("done");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16);
  struct Rec { std::string name; json input; std::string result; bool err; };
  std::vector<Rec> recs;
  loop.setOnToolCall([&](const std::string& n, const json& in, const std::string& r, bool e) {
    recs.push_back({n, in, r, e});
  });
  std::vector<Message> msgs = {userText("go")};
  loop.run(msgs);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_EQ(recs[0].name, "echo");
  EXPECT_EQ(recs[0].input.value("v", std::string("")), "RESULT");
  EXPECT_EQ(recs[0].result, "RESULT");  // echoTool 回显 in["v"]
  EXPECT_FALSE(recs[0].err);
}

TEST(AgentLoop, OnToolCallReportsPermissionDenied) {
  // 被拒的工具也要上报（isError=true，结果含 permission denied），用户才知道发生了什么。
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "danger", json{{"v", "X"}}) : textResp("done");
  });
  ToolRegistry reg = registryWith(dangerTool());
  AgentLoop loop(fake, reg, 16);
  std::string nm;
  bool sawErr = false;
  loop.setOnToolCall([&](const std::string& n, const json&, const std::string& r, bool e) {
    nm = n;
    if (e && r.find("permission denied") != std::string::npos) sawErr = true;
  });
  std::vector<Message> msgs = {userText("go")};
  loop.run(msgs, {}, [](const std::string&, const json&) { return false; });
  EXPECT_EQ(nm, "danger");
  EXPECT_TRUE(sawErr);
}

// ---- 反思（reflection）----

TEST(AgentLoop, SelfCheckInsertsReflectionAfterTool) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "R"}}) : textResp("final");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16, /*selfCheck=*/true);
  std::vector<Message> msgs = {userText("use tool")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(reply, "final");
  EXPECT_EQ(fake.calls_, 2);
  // 工具结果消息（Role::Tool）之后应紧跟一条自检 Role::User 消息
  bool selfCheckAfterTool = false;
  for (size_t i = 0; i + 1 < msgs.size(); ++i)
    if (msgs[i].role == Role::Tool && msgs[i + 1].role == Role::User)
      selfCheckAfterTool = true;
  EXPECT_TRUE(selfCheckAfterTool);
}

TEST(AgentLoop, NoSelfCheckByDefault) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "R"}}) : textResp("final");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16);  // selfCheck 默认关
  std::vector<Message> msgs = {userText("use tool")};
  loop.run(msgs);
  // 不自检：Tool 消息后不应紧跟 User 消息
  for (size_t i = 0; i + 1 < msgs.size(); ++i)
    if (msgs[i].role == Role::Tool)
      EXPECT_NE(msgs[i + 1].role, Role::User);
}

TEST(AgentLoop, ReflectionFeedsCritiqueBackAndStopsWhenSatisfied) {
  FakeLlmClient fake([](int i) {
    switch (i) {
      case 0:  return textResp("draft1");        // round0 生成
      case 1:  return textResp("需要补充细节");   // round0 评论家：不满意
      case 2:  return textResp("draft2");        // round1 生成（据批评修订）
      default: return textResp("答案已经很好");   // round1 评论家：满意 → 停
    }
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("write")};
  std::string reply = loop.runWithReflection(msgs, /*maxRounds=*/5);

  EXPECT_EQ(reply, "draft2");
  EXPECT_EQ(fake.calls_, 4);
  // 批评意见应作为 Role::User 回灌
  bool fedBack = false;
  for (const auto& m : msgs)
    if (m.role == Role::User)
      for (const auto& b : m.content)
        if (auto* tb = std::get_if<TextBlock>(&b))
          if (tb->text.find("批评意见") != std::string::npos &&
              tb->text.find("需要补充细节") != std::string::npos)
            fedBack = true;
  EXPECT_TRUE(fedBack);
}

TEST(AgentLoop, ReflectionRespectsMaxRounds) {
  FakeLlmClient fake([](int i) {
    return (i % 2 == 0) ? textResp("draft") : textResp("还要改");  // 评论家永不满意
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("write")};
  std::string reply = loop.runWithReflection(msgs, /*maxRounds=*/2);

  EXPECT_EQ(reply, "draft");
  EXPECT_EQ(fake.calls_, 3);  // run, critic, run（最后一轮不再批判）
}

// ---- Plan-and-Execute ----

namespace {
// 统计 messages 中"执行步骤"开头的 User 文本消息数。
int countStepMessages(const std::vector<Message>& msgs) {
  int n = 0;
  for (const auto& m : msgs)
    if (m.role == Role::User)
      for (const auto& b : m.content)
        if (auto* tb = std::get_if<TextBlock>(&b))
          if (tb->text.rfind("执行步骤", 0) == 0) n++;
  return n;
}
}  // namespace

TEST(AgentLoop, PlanExecuteRunsPlannedSteps) {
  FakeLlmClient fake([](int i) {
    switch (i) {
      case 0:  return textResp(R"({"steps":["A","B"]})");  // 规划
      case 1:  return textResp("did A");                   // 步骤1
      case 2:  return textResp("did B");                   // 步骤2
      default: return textResp("综合答案");                // 综合
    }
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("multi-step task")};
  std::string plan;
  std::string reply = loop.runPlanExecute(
      msgs, /*maxSteps=*/6, {}, {},
      [&](const std::string& p) { plan = p; });

  EXPECT_EQ(reply, "综合答案");
  EXPECT_EQ(fake.calls_, 4);          // 规划 + 2 步 + 综合
  EXPECT_EQ(countStepMessages(msgs), 2);
  EXPECT_NE(plan.find("A"), std::string::npos);  // onInfo 收到了计划
  EXPECT_NE(plan.find("B"), std::string::npos);
}

TEST(AgentLoop, PlanExecuteFallsBackWhenNoPlan) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? textResp("你好，有什么可以帮你？")  // 非 JSON、无步骤
                  : textResp("final answer");
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("hi")};
  std::string reply = loop.runPlanExecute(msgs, /*maxSteps=*/6);

  EXPECT_EQ(reply, "final answer");  // 退回普通 run
  EXPECT_EQ(fake.calls_, 2);         // 规划(空) + run
  EXPECT_EQ(countStepMessages(msgs), 0);
}

TEST(AgentLoop, PlanExecuteRespectsMaxSteps) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? textResp(R"({"steps":["A","B","C","D","E"]})")
                  : textResp("step/synthesis output");
  });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("big task")};
  loop.runPlanExecute(msgs, /*maxSteps=*/2);

  EXPECT_EQ(countStepMessages(msgs), 2);  // 5 步计划，但只执行 2 步
  EXPECT_EQ(fake.calls_, 4);              // 规划 + 2 步 + 综合
}
