#include "core/AgentLoop.h"
#include "core/ToolInterceptor.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace aicoder {

namespace {
// 每步工具后的自检提示（注入为 Role::User，模型下一轮先反思再决定）。
constexpr const char* kSelfCheckPrompt =
    "反思：以上工具结果是否足以回答用户的问题？还缺什么？"
    "据此决定——继续调用工具，或给出最终答案。";

// 整体答案批判的"评论家"系统提示。
constexpr const char* kCriticPrompt =
    "你是一位严格的评论家。请批判性地审视上面这条回答："
    "准确性、完整性、逻辑性、表达是否清晰、有无冗余、是否遗漏关键信息。"
    "只输出具体的批评和改进建议，不要重写答案。"
    "如果回答已经足够好，请明确回复\"答案已经很好，无需修改\"。";

// 评论家是否认可（认可则停止外层反思循环）。
bool reflectionSatisfied(const std::string& critique) {
  return critique.find("答案已经很好") != std::string::npos ||
         critique.find("无需修改") != std::string::npos;
}

// 规划阶段系统提示：只输出 JSON 步骤列表。
constexpr const char* kPlannerPrompt =
    "你是规划助手。把用户的任务分解为有序、可执行的步骤。"
    "只输出 JSON，格式为 {\"steps\": [\"步骤1\", \"步骤2\", ...]}，不要输出其它内容。"
    "若任务无需分步（如闲聊或简单问答），输出 {\"steps\": []}。";

// 全部步骤执行完后，综合最终答案。
constexpr const char* kSynthesizePrompt =
    "以上各步骤已执行完毕。请综合所有步骤的结果，给出对用户原始问题的完整最终答案。";

// 解析规划返回文本为步骤列表：优先 JSON {"steps":[...]}，否则只收带编号/项目符号的行
// （避免把普通散文误当成步骤）。解析不出则返回空。
std::vector<std::string> parsePlanSteps(const std::string& text) {
  std::vector<std::string> steps;
  size_t l = text.find('{'), r = text.rfind('}');
  if (l != std::string::npos && r != std::string::npos && r > l) {
    try {
      json j = json::parse(text.substr(l, r - l + 1));
      if (j.contains("steps") && j["steps"].is_array()) {
        for (const auto& s : j["steps"])
          if (s.is_string() && !s.get<std::string>().empty())
            steps.push_back(s.get<std::string>());
        return steps;  // JSON 命中即采用（空数组 = 无需分步）
      }
    } catch (const json::exception&) { /* 落到逐行解析 */ }
  }
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    size_t b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;
    size_t e = line.find_last_not_of(" \t\r");
    std::string t = line.substr(b, e - b + 1);
    size_t k = 0;
    while (k < t.size() && std::isdigit((unsigned char)t[k])) k++;
    bool numbered = (k > 0 && k < t.size() && (t[k] == '.' || t[k] == ')'));
    bool bullet = (t.size() >= 2 && (t[0] == '-' || t[0] == '*') && t[1] == ' ');
    if (numbered) k++;
    else if (bullet) k = 1;
    else continue;  // 非步骤行，跳过
    while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) k++;
    if (k < t.size()) steps.push_back(t.substr(k));
  }
  return steps;
}

// 把步骤列表格式化为可读计划文本。
std::string formatPlan(const std::vector<std::string>& steps) {
  std::string out = "计划:";
  for (size_t i = 0; i < steps.size(); ++i)
    out += "\n" + std::to_string(i + 1) + ". " + steps[i];
  return out;
}
}  // namespace

AgentLoop::AgentLoop(LlmClient& client, const ToolRegistry& registry, int maxIterations,
                     bool selfCheck)
    : client_(client), registry_(registry), maxIterations_(maxIterations),
      selfCheck_(selfCheck) {}

std::string AgentLoop::run(std::vector<Message>& messages,
                           const LlmClient::DeltaCallback& onDelta,
                           const LlmClient::PermissionCallback& onPermission) {
  LlmClient::DeltaCallback cb =
      onDelta ? onDelta : [](const StreamDelta&) {};
  LlmClient::PermissionCallback permCb =
      onPermission ? onPermission : [](const std::string&, const json&) { return true; };

  for (int iter = 0; iter < maxIterations_; ++iter) {
    Response resp = client_.sendStream(messages, registry_.specs(), cb);
    messages.push_back(resp.assistant_message);

    std::vector<ToolUseBlock> toolUses;
    for (const auto& block : resp.assistant_message.content)
      if (const auto* tu = std::get_if<ToolUseBlock>(&block))
        toolUses.push_back(*tu);

    if (toolUses.empty())
      return assistantText(resp.assistant_message);

    // 拦截器：把 LLM 一次性返回的 toolUses 串行处理,任一 Deny/失败
    // 会中断队列,后续工具标记为 skipped。保留旧 permCb 签名(bool)→
    // 在拦截器内转成 AskResult {Allow, Deny};AllowForever 由持久化层
    // 自行提供 AllowLookup,UI 路径会在 ReplView 侧接线。
    ToolInterceptor interceptor;
    interceptor.setMetaLookup([this](const std::string& name) {
      return ToolMeta{name, registry_.needsPermission(name)};
    });
    interceptor.setAsker([&permCb](const ToolUseBlock& tu, const std::string& name) {
      // 旧 API 只暴露 bool,无 AllowForever 路径。持久化白名单由
      // AllowLookup 提供,这里 Deny 时直接中止队列。
      return permCb(name, tu.input) ? AskResult::Allow : AskResult::Deny;
    });
    interceptor.setExecutor([this](const ToolUseBlock& tu) {
      return registry_.invoke(tu.id, tu.name, tu.input);
    });
    interceptor.setStepReporter([this](const ToolUseBlock& tu, const ToolResultBlock& r) {
      if (onToolCall_)
        onToolCall_(tu.name, tu.input, r.content, r.is_error);
    });

    auto results = interceptor.intercept(toolUses);
    Message toolMsg{Role::Tool, {}};
    for (auto& r : results)
      toolMsg.content.push_back(std::move(r));
    messages.push_back(std::move(toolMsg));

    // 每步工具后的自检：注入反思提示，模型下一轮先反思再决定继续/收尾。
    if (selfCheck_)
      messages.push_back(userText(kSelfCheckPrompt));
  }

  const std::string note = "[已达到最大迭代次数，已停止]";
  messages.push_back(Message{Role::Assistant, {TextBlock{note}}});
  return note;
}

std::string AgentLoop::criticReview(const std::vector<Message>& messages) {
  std::vector<Message> critic;
  critic.push_back(systemText(kCriticPrompt));
  // 带上对话（去掉原有 system prompt，避免双 system），让评论家在上下文中评判。
  for (const auto& m : messages)
    if (m.role != Role::System) critic.push_back(m);
  Response resp = client_.send(critic, {});  // 评论家不带工具
  return assistantText(resp.assistant_message);
}

std::string AgentLoop::runWithReflection(std::vector<Message>& messages, int maxRounds,
                                         const LlmClient::DeltaCallback& onDelta,
                                         const LlmClient::PermissionCallback& onPermission) {
  std::string draft;
  for (int round = 0; round < maxRounds; ++round) {
    draft = run(messages, onDelta, onPermission);  // 生成（内层 run 含每步自检）
    if (round + 1 >= maxRounds) break;             // 最后一轮无需再批判
    std::string critique = criticReview(messages);
    if (reflectionSatisfied(critique)) break;
    messages.push_back(userText("批评意见: " + critique));  // 回灌 → 下一轮据此修订
  }
  return draft;
}

std::vector<std::string> AgentLoop::planSteps(const std::vector<Message>& messages) {
  std::vector<Message> planner;
  planner.push_back(systemText(kPlannerPrompt));
  // 去掉原 system，避免双 system；其余对话作为规划上下文。
  for (const auto& m : messages)
    if (m.role != Role::System) planner.push_back(m);
  Response resp = client_.send(planner, {});  // 规划不带工具
  return parsePlanSteps(assistantText(resp.assistant_message));
}

std::string AgentLoop::runPlanExecute(std::vector<Message>& messages, int maxSteps,
                                      const LlmClient::DeltaCallback& onDelta,
                                      const LlmClient::PermissionCallback& onPermission,
                                      const InfoCallback& onInfo) {
  std::vector<std::string> steps = planSteps(messages);
  if (steps.empty())
    return run(messages, onDelta, onPermission);  // 无法/无需规划 → 退回普通 agent

  std::string plan = formatPlan(steps);
  messages.push_back(Message{Role::Assistant, {TextBlock{plan}}});  // 模型可见自己的计划
  if (onInfo) onInfo(plan);                                          // UI 展示计划

  int n = std::min(static_cast<int>(steps.size()), maxSteps);
  for (int i = 0; i < n; ++i) {
    std::string header = "执行步骤 " + std::to_string(i + 1) + "/" +
                         std::to_string(n) + ": " + steps[i] +
                         "\n只完成这一步，完成后简述结果。";
    messages.push_back(userText(header));
    run(messages, onDelta, onPermission);  // 执行器：复用现有循环，步骤内可用工具
  }

  messages.push_back(userText(kSynthesizePrompt));
  return run(messages, onDelta, onPermission);  // 综合最终答案
}

}