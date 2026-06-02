#pragma once
#include <string>
#include <vector>
#include "core/Message.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"

namespace aicoder
{

  class AgentLoop
  {
  public:
    // selfCheck=true 时，每执行完一步工具就追加一句"自检"反思提示，让模型下一轮先反思
    // 观察结果是否充分、再决定继续调用工具或收尾（不额外调用模型，复用现有循环）。
    AgentLoop(LlmClient &client, const ToolRegistry &registry, int maxIterations,
              bool selfCheck = false);
    // 迭代直到模型不再要工具（end_turn）或撞上限；追加消息到 messages；返回最终 assistant 文本。
    // onDelta（可选）：流式显示增量回调；不传则非流式语义（只在结束后拿完整结果）。
    // 基建失败时 client.sendStream 抛 LlmError，由调用方处理。
    // onPermission（可选）：工具执行前回调，要求用户确认；不传则默认允许。
    std::string run(std::vector<Message> &messages,
                    const LlmClient::DeltaCallback &onDelta = {},
                    const LlmClient::PermissionCallback &onPermission = {});
    // 外层反思：把 run 当"生成器"，每轮跑完由"评论家"批判整体答案，批评以 Role::User 回灌后
    // 再跑一轮修订，直到评论家认可（"答案已经很好"）或达到 maxRounds。返回最后一版草稿。
    // 评论家调用不带工具、用独立消息列表（不污染主对话的 system prompt）。
    std::string runWithReflection(std::vector<Message> &messages, int maxRounds,
                                  const LlmClient::DeltaCallback &onDelta = {},
                                  const LlmClient::PermissionCallback &onPermission = {});
    // Plan-and-Execute（简单变体）：先规划出有序步骤 → 逐步执行（每步复用 run，可用工具）
    // → 综合最终答案，不做重规划。onInfo（可选）把"计划"等阶段性信息作为独立消息推给 UI。
    using InfoCallback = std::function<void(const std::string &text)>;
    std::string runPlanExecute(std::vector<Message> &messages, int maxSteps,
                               const LlmClient::DeltaCallback &onDelta = {},
                               const LlmClient::PermissionCallback &onPermission = {},
                               const InfoCallback &onInfo = {});
    // 运行时切换"每步工具自检"（供 UI 模式切换调用）。
    void setSelfCheck(bool v) { selfCheck_ = v; }
    // 工具调用观察回调：每次工具执行后触发，向 UI 报告调了什么工具、参数、结果及是否出错。
    // 对 run / runWithReflection / runPlanExecute 三种模式统一生效（都复用 run 的工具循环）。
    using ToolCallCallback = std::function<void(const std::string &name, const json &input,
                                                const std::string &result, bool isError)>;
    void setOnToolCall(ToolCallCallback cb) { onToolCall_ = std::move(cb); }

  private:
    // 让"评论家"审视当前对话并返回批评文本。
    std::string criticReview(const std::vector<Message> &messages);
    // 规划阶段：让模型（不带工具）产出有序步骤列表（解析 JSON {"steps":[...]} 或逐行）。
    std::vector<std::string> planSteps(const std::vector<Message> &messages);

  private:
    LlmClient &client_;
    const ToolRegistry &registry_;
    int maxIterations_;
    bool selfCheck_;
    ToolCallCallback onToolCall_;
  };

}
