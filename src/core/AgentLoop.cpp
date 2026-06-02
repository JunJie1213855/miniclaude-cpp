#include "core/AgentLoop.h"
#include "core/ToolInterceptor.h"
#include "core/Errors.h"
#include "llm/DefaultLlmClient.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <string>
#include <thread>

namespace aicoder {

namespace {
// 每步工具后的自检提示(注入为 Role::User,模型下一轮先反思再决定)。
constexpr const char* kSelfCheckPrompt =
    "反思:以上工具结果是否足以回答用户的问题?还缺什么?"
    "据此决定——继续调用工具,或给出最终答案。";

// 整体答案批判的"评论家"系统提示。
constexpr const char* kCriticPrompt =
    "你是一位严格的评论家。请批判性地审视上面这条回答:"
    "准确性、完整性、逻辑性、表达是否清晰、有无冗余、是否遗漏关键信息。"
    "只输出具体的批评和改进建议,不要重写答案。"
    "如果回答已经足够好,请明确回复\"答案已经很好,无需修改\"。";

// ⑤ 迭代上限警告文本。
constexpr const char* kMaxIterNote =
    "[已达到最大迭代次数,已停止。请总结当前已知结果或减少工具调用。]";

// ④ 用户取消时返回的占位文本。
constexpr const char* kCancelNote = "[已取消,残缺消息已保留]";

// ② 压缩时的占位(给用户提示历史被裁剪)。
constexpr const char* kCompactionMarker = "\n[truncated by compaction]";

// 评论家是否认可(认可则停止外层反思循环)。
bool reflectionSatisfied(const std::string& critique) {
  return critique.find("答案已经很好") != std::string::npos ||
         critique.find("无需修改") != std::string::npos;
}

// 规划阶段系统提示:只输出 JSON 步骤列表。
constexpr const char* kPlannerPrompt =
    "你是规划助手。把用户的任务分解为有序、可执行的步骤。"
    "只输出 JSON,格式为 {\"steps\": [\"步骤1\", \"步骤2\", ...]},不要输出其它内容。"
    "若任务无需分步(如闲聊或简单问答),输出 {\"steps\": []}。";

// 全部步骤执行完后,综合最终答案。
constexpr const char* kSynthesizePrompt =
    "以上各步骤已执行完毕。请综合所有步骤的结果,给出对用户原始问题的完整最终答案。";

// 解析规划返回文本为步骤列表:优先 JSON {"steps":[...]},否则只收带编号/项目符号的行
// (避免把普通散文误当成步骤)。解析不出则返回空。
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
        return steps;  // JSON 命中即采用(空数组 = 无需分步)
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
    else continue;  // 非步骤行,跳过
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

// ④ 取消时,cancel 触发的 HTTP 中断走 LlmError(curl 报 CURLE_ABORTED_BY_CALLBACK)。
// 这里把这种特殊情况翻译成更友好的提示;其它 LlmError(网络/超时)保留原文案。
std::string friendlyCancelError(const std::string& raw) {
  return std::string(kCancelNote) + " (cause: " + raw + ")";
}

}  // namespace

AgentLoop::AgentLoop(LlmClient& client, const ToolRegistry& registry, int maxIterations,
                     bool selfCheck)
    : client_(client), registry_(registry), maxIterations_(maxIterations),
      selfCheck_(selfCheck) {}

// ============== ② Context Compaction ==============

size_t AgentLoop::messagesSerializedSize(const std::vector<Message> &msgs) {
  // 用 json dump 估算序列化字节(开销:每次都序列化一次,O(n);
  // messages 数量级通常 < 100,足以胜任;真要高频调可以加缓存)。
  json j = json::array();
  for (const auto& m : msgs) {
    json item;
    item["role"] = static_cast<int>(m.role);
    item["rc"] = m.reasoning_content;
    json blocks = json::array();
    for (const auto& b : m.content) {
      if (const auto* t = std::get_if<TextBlock>(&b)) {
        blocks.push_back({{"type", "text"}, {"t", t->text}});
      } else if (const auto* u = std::get_if<ToolUseBlock>(&b)) {
        blocks.push_back({{"type", "tool_use"}, {"id", u->id}, {"name", u->name}, {"input", u->input}});
      } else if (const auto* r = std::get_if<ToolResultBlock>(&b)) {
        blocks.push_back({{"type", "tool_result"}, {"id", r->tool_use_id}, {"c", r->content}, {"err", r->is_error}});
      }
    }
    item["content"] = blocks;
    j.push_back(item);
  }
  return j.dump().size();
}

bool AgentLoop::needCompaction(const std::vector<Message> &msgs) {
  if (msgs.size() <= AgentLoop::kCompactionKeepRecent + 1) return false;
  return messagesSerializedSize(msgs) > AgentLoop::kCompactionThresholdBytes;
}

void AgentLoop::compactMessages(std::vector<Message> &msgs) {
  // 策略:
  //   1) 保留首条 system(若存在)+ 最近 kCompactionKeepRecent 条
  //   2) 中间 ToolResultBlock.content 截断:错误块 -> 200B,非错误 -> 500B
  //   3) TextBlock / ToolUseBlock 不动
  if (msgs.size() <= kCompactionKeepRecent + 1) return;
  const size_t keep_recent = kCompactionKeepRecent;
  const size_t split = msgs.size() > keep_recent ? msgs.size() - keep_recent : 0;
  // 找首个非 system 的下标,作为"中间段"起点。
  size_t first_non_system = 0;
  for (size_t i = 0; i < split; ++i) {
    if (msgs[i].role != Role::System) { first_non_system = i; break; }
    // System 在前半段,保留;若前半段全 system,first_non_system = split(全裁)
  }
  // 系统消息保留在原位;中间段 [first_non_system, split) 全部按规则裁。
  for (size_t i = first_non_system; i < split; ++i) {
    for (auto& b : msgs[i].content) {
      if (auto* r = std::get_if<ToolResultBlock>(&b)) {
        const size_t cap = r->is_error ? kCompactionTruncateErrorBytes
                                       : kCompactionTruncateBytes;
        if (r->content.size() > cap) {
          r->content = r->content.substr(0, cap) + kCompactionMarker;
        }
      }
    }
  }
  // 提示用户一次:把"已压缩"作为系统注释附加到最近消息末尾(不影响大对话历史)。
  if (!msgs.empty()) {
    Message note;
    note.role = Role::User;
    note.content.push_back(TextBlock{"[系统提示: 历史已自动压缩以避免上下文超限]"});
    msgs.insert(msgs.end() - 1, std::move(note));
  }
}

void AgentLoop::injectMaxIterNote(std::vector<Message> &msgs) {
  Message note{Role::Assistant, {TextBlock{kMaxIterNote}}};
  msgs.push_back(std::move(note));
}

// ============== 主循环(5 类边界) ==============

std::string AgentLoop::run(std::vector<Message>& messages,
                           const LlmClient::DeltaCallback& onDelta,
                           const LlmClient::PermissionCallback& onPermission) {
  LlmClient::DeltaCallback cb =
      onDelta ? onDelta : [](const StreamDelta&) {};
  LlmClient::PermissionCallback permCb =
      onPermission ? onPermission : [](const std::string&, const json&) { return true; };

  for (int iter = 0; iter < maxIterations_; ++iter) {
    // ④ 循环顶端检查取消
    if (cancelRequested()) {
      messages.push_back(Message{Role::Assistant, {TextBlock{kCancelNote}}});
      return kCancelNote;
    }

    // ② 压缩检查
    if (needCompaction(messages))
      compactMessages(messages);

    Response resp;
    try {
      // 透传 cancel 到 sendStream(若有)。DefaultLlmClient 暴露
      // sendStream(...,cancel);基类无 cancel 版本,这里 dynamic_cast 一下。
      if (auto* d = dynamic_cast<DefaultLlmClient*>(&client_)) {
        resp = d->sendStream(messages, registry_.specs(), cb, cancel_.get());
      } else {
        resp = client_.sendStream(messages, registry_.specs(), cb);
      }
    } catch (const LlmError& e) {
      // ④ 取消导致的 CURLE_ABORTED_BY_CALLBACK 也走这里。
      const std::string what = e.what();
      const bool aborted = (what.find("CURLE_ABORTED_BY_CALLBACK") != std::string::npos ||
                            what.find("aborted") != std::string::npos ||
                            cancelRequested());
      const std::string note = aborted ? friendlyCancelError(what)
                                       : (std::string("[网络中断: ") + what + "]");
      messages.push_back(Message{Role::Assistant, {TextBlock{note}}});
      return note;
    } catch (const std::exception& e) {
      // 其它非 LlmError(比如 std::bad_alloc 在巨型 messages 上):也不致命
      const std::string note = std::string("[未知错误: ") + e.what() + "]";
      messages.push_back(Message{Role::Assistant, {TextBlock{note}}});
      return note;
    }

    messages.push_back(resp.assistant_message);

    // ① 正常终止:无 tool_use → 退出
    std::vector<ToolUseBlock> toolUses;
    for (const auto& block : resp.assistant_message.content)
      if (const auto* tu = std::get_if<ToolUseBlock>(&block))
        toolUses.push_back(*tu);

    if (toolUses.empty())
      return assistantText(resp.assistant_message);

    // ③ 工具错误自修:走拦截器。ToolRegistry::invoke 内部把 ToolError
    // 捕获并转 is_error=true,is_error 的 result 通过 messages 推回 LLM;
    // selfCheck_ 开启时每步后追加反思 prompt,引导下一轮先反思。
    ToolInterceptor interceptor;
    interceptor.setMetaLookup([this](const std::string& name) {
      return ToolMeta{name, registry_.needsPermission(name)};
    });
    interceptor.setAsker([&permCb](const ToolUseBlock& tu, const std::string& name) {
      return permCb(name, tu.input) ? AskResult::Allow : AskResult::Deny;
    });
    interceptor.setExecutor([this](const ToolUseBlock& tu) {
      return registry_.invoke(tu.id, tu.name, tu.input);
    });
    interceptor.setBetweenAsksYield([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(60));
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

    if (selfCheck_)
      messages.push_back(userText(kSelfCheckPrompt));
  }

  // ⑤ 撞上限:注入警告 + 返回提示文本
  injectMaxIterNote(messages);
  return kMaxIterNote;
}

std::string AgentLoop::criticReview(const std::vector<Message>& messages) {
  std::vector<Message> critic;
  critic.push_back(systemText(kCriticPrompt));
  for (const auto& m : messages)
    if (m.role != Role::System) critic.push_back(m);
  Response resp = client_.send(critic, {});
  return assistantText(resp.assistant_message);
}

std::string AgentLoop::runWithReflection(std::vector<Message>& messages, int maxRounds,
                                         const LlmClient::DeltaCallback& onDelta,
                                         const LlmClient::PermissionCallback& onPermission) {
  std::string draft;
  for (int round = 0; round < maxRounds; ++round) {
    draft = run(messages, onDelta, onPermission);
    if (round + 1 >= maxRounds) break;
    std::string critique = criticReview(messages);
    if (reflectionSatisfied(critique)) break;
    messages.push_back(userText("批评意见: " + critique));
  }
  return draft;
}

std::vector<std::string> AgentLoop::planSteps(const std::vector<Message>& messages) {
  std::vector<Message> planner;
  planner.push_back(systemText(kPlannerPrompt));
  for (const auto& m : messages)
    if (m.role != Role::System) planner.push_back(m);
  Response resp = client_.send(planner, {});
  return parsePlanSteps(assistantText(resp.assistant_message));
}

std::string AgentLoop::runPlanExecute(std::vector<Message>& messages, int maxSteps,
                                      const LlmClient::DeltaCallback& onDelta,
                                      const LlmClient::PermissionCallback& onPermission,
                                      const InfoCallback& onInfo) {
  std::vector<std::string> steps = planSteps(messages);
  if (steps.empty())
    return run(messages, onDelta, onPermission);

  std::string plan = formatPlan(steps);
  messages.push_back(Message{Role::Assistant, {TextBlock{plan}}});
  if (onInfo) onInfo(plan);

  int n = std::min(static_cast<int>(steps.size()), maxSteps);
  for (int i = 0; i < n; ++i) {
    std::string header = "执行步骤 " + std::to_string(i + 1) + "/" +
                         std::to_string(n) + ": " + steps[i] +
                         "\n只完成这一步,完成后简述结果。";
    messages.push_back(userText(header));
    run(messages, onDelta, onPermission);
  }

  messages.push_back(userText(kSynthesizePrompt));
  return run(messages, onDelta, onPermission);
}

}
