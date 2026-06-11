#pragma once
#include <atomic>
#include <memory>
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
    // selfCheck=true 时,每执行完一步工具就追加一句"自检"反思提示,让模型下一轮先反思
    // 观察结果是否充分、再决定继续调用工具或收尾(不额外调用模型,复用现有循环)。
    AgentLoop(LlmClient &client, const ToolRegistry &registry, int maxIterations,
              bool selfCheck = false);
    // 迭代直到模型不再要工具(end_turn)或撞上限;追加消息到 messages;返回最终 assistant 文本。
    // onDelta(可选):流式显示增量回调;不传则非流式语义(只在结束后拿完整结果)。
    // 基建失败时 client.sendStream 抛 LlmError,由调用方处理。
    // onPermission(可选):工具执行前回调,要求用户确认;不传则默认允许。
    // 5 类边界:
    //   ① 正常终止:工具列表为空 → 退出 + 返回助手文本
    //   ② 上下文压缩:messages 序列化字节 > kCompactionThresholdBytes → 裁剪中间
    //   ③ 工具错误自修:is_error=true 不跳出,自检 prompt 引导下一轮反思
    //   ④ 用户主动中断:cancel token(原子 bool) → curl progress 回调掐断 HTTP
    //   ⑤ 迭代上限硬死锁:iter >= maxIter → 注入警告消息 + 退出
    std::string run(std::vector<Message> &messages,
                    const LlmClient::DeltaCallback &onDelta = {},
                    const LlmClient::PermissionCallback &onPermission = {});

    // 取消信号(可选):UI 线程可注入一个 atomic<bool>,run 会:
    //   1) 每次循环顶端检查
    //   2) 透传到 DefaultLlmClient::sendStream → HttpTransport → curl progress callback
    // cancel == nullptr 时不检查(老调用零侵入)。
    void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel) { cancel_ = std::move(cancel); }
    bool cancelRequested() const {
      return cancel_ && cancel_->load(std::memory_order_relaxed);
    }

    // 外层反思:把 run 当"生成器",每轮跑完由"评论家"批判整体答案,批评以 Role::User 回灌后
    // 再跑一轮修订,直到评论家认可("答案已经很好")或达到 maxRounds。返回最后一版草稿。
    // 评论家调用不带工具、用独立消息列表(不污染主对话的 system prompt)。
    std::string runWithReflection(std::vector<Message> &messages, int maxRounds,
                                  const LlmClient::DeltaCallback &onDelta = {},
                                  const LlmClient::PermissionCallback &onPermission = {});

    // Plan-and-Execute(简单变体):先规划出有序步骤 → 逐步执行(每步复用 run,可用工具)
    // → 综合最终答案,不做重规划。onInfo(可选)把"计划"等阶段性信息作为独立消息推给 UI。
    using InfoCallback = std::function<void(const std::string &text)>;
    std::string runPlanExecute(std::vector<Message> &messages, int maxSteps,
                               const LlmClient::DeltaCallback &onDelta = {},
                               const LlmClient::PermissionCallback &onPermission = {},
                               const InfoCallback &onInfo = {});

    // 运行时切换"每步工具自检"(供 UI 模式切换调用)。
    void setSelfCheck(bool v) { selfCheck_ = v; }
    // 暴露底层 LLM 客户端(用于 /create_* 这类需要直接调 LLM 的辅助流程)。
    LlmClient &client() { return client_; }
    // 工具调用观察回调:每次工具执行后触发,向 UI 报告调了什么工具、参数、结果及是否出错。
    // 对 run / runWithReflection / runPlanExecute 三种模式统一生效(都复用 run 的工具循环)。
    using ToolCallCallback = std::function<void(const std::string &name, const json &input,
                                                const std::string &result, bool isError)>;
    void setOnToolCall(ToolCallCallback cb) { onToolCall_ = std::move(cb); }

    // 工具调用预渲染回调:在 LLM 完整消息(含 thinking+text+tool_use)收完、工具执行**前**触发,
    // 让 UI 能在工具执行前先显示 "⚙ tool_name ..." 行,体现"消息已收完,准备调工具"的过渡。
    using ToolUsePreviewCallback = std::function<void(const std::vector<ToolUseBlock> &)>;
    void setToolUsePreview(ToolUsePreviewCallback cb) { onToolUsePreview_ = std::move(cb); }

    // 上下文压缩阈值(序列化字节,粗略按 ~4 字符/token 估)。
    // 默认 80 KB,留 16 KB 余量给 96 K context 模型。
    static constexpr size_t kCompactionThresholdBytes = 80 * 1024;
    // 压缩后保留最近 N 轮对话,其余历史 ToolResultBlock 截断。
    static constexpr size_t kCompactionKeepRecent = 6;
    // 压缩时非错误工具结果 content 截断到这么多字符。
    static constexpr size_t kCompactionTruncateBytes = 500;
    // 压缩时错误工具结果 content 截断(冗长堆栈可丢)。
    static constexpr size_t kCompactionTruncateErrorBytes = 200;

  private:
    // 让"评论家"审视当前对话并返回批评文本。
    std::string criticReview(const std::vector<Message> &messages);
    // 规划阶段:让模型(不带工具)产出有序步骤列表(解析 JSON {"steps":[...]} 或逐行)。
    std::vector<std::string> planSteps(const std::vector<Message> &messages);

    // ========== query / query_loop 分层抽象(参考 query_learn.md) ==========
    //
    // 分层职责:
    //   query_loop: 单轮核心。一次 sendStream → 解析 Response → 拦截器执行工具
    //               → tool_result 回填 messages。selfCheck 触发的额外模型往返
    //               在内部 while 中消化。不做:取消顶端检查、压缩、MaxIter 注入、
    //               正常终止 return。
    //   query:      多轮组装。while(iter<maxIter) 顶端 4 守卫(cancel/compaction
    //               /query_loop/正常终止),把 5 类循环边界都收在这层。
    //   run:        薄壳。try { query } + LlmError 翻译(取消/网络/未知)。
    //               wrapper (runWithReflection / runPlanExecute) 继续调 run(),不感知分层。
    //
    // query_loop 返回的元信息:query 用来判"还要不要再来一轮"。
    struct QueryLoopResult
    {
      bool has_tool_use = false; // 是否有 tool_use 块(query 用此判正常终止)
      bool cancelled = false;    // 取消是否在 query_loop 内已被透传
      std::string final_text;    // 助手最终文本(纯文本轮时填,便于 query 直接透出)
    };

    QueryLoopResult query_loop(std::vector<Message> &messages,
                               const LlmClient::DeltaCallback &onDelta,
                               const LlmClient::PermissionCallback &onPermission);

    void query(std::vector<Message> &messages,
               const LlmClient::DeltaCallback &onDelta,
               const LlmClient::PermissionCallback &onPermission);

  public:
    // ② 上下文压缩(public 暴露以便单元测试 + 调试 hook)。
    // 策略:保留 system + 最近 kCompactionKeepRecent 条,
    // 中间 ToolResultBlock 截断 / 错误块更狠。
    static size_t messagesSerializedSize(const std::vector<Message> &msgs);
    static bool needCompaction(const std::vector<Message> &msgs);
    static void compactMessages(std::vector<Message> &msgs);

    // ⑤ 注入"已达上限"警告助手消息。
    static void injectMaxIterNote(std::vector<Message> &msgs);

  private:
    LlmClient &client_;
    const ToolRegistry &registry_;
    int maxIterations_;
    bool selfCheck_;
    ToolCallCallback onToolCall_;
    ToolUsePreviewCallback onToolUsePreview_;
    std::shared_ptr<std::atomic<bool>> cancel_;  // ④ 取消信号
  };

}
