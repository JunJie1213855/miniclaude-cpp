#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"

namespace aicoder
{

  // UI 给执行器的"询问一次"回调。返回 Allow / AllowForever / Deny。
  enum class AskResult
  {
    Allow,
    AllowForever,
    Deny
  };

  // 一个工具的元信息(让执行器能决定"是否需要询问",而不直接 import ToolRegistry)。
  struct ToolMeta
  {
    std::string name;
    bool needsPermission = false;
  };

  // 执行器配置:每个回调都能注入(便于测试替换)。
  // 职责:对一组 ToolUseBlock 做权限询问 + 顺序执行 + 中止语义。
  // 参考 Claude Code StreamingToolExecutor:纯流式入队(enqueue) + 顺序执行(run),
  // 调用方模式:
  //   for (const auto& tu : toolUses) ic.enqueue(tu);
  //   auto results = ic.run();
  class StreamToolExecutor
  {
  public:
    // 真正执行一个工具:返回 ToolResultBlock(已含异常转 is_error=true)。
    using Executor = std::function<ToolResultBlock(const ToolUseBlock &)>;

    // 弹窗询问:返回 Allow / AllowForever / Deny;若为 AllowForever,
    // 执行器会调 onAllowForever 把规则写进持久化。
    using Asker = std::function<AskResult(const ToolUseBlock &, const std::string &toolName)>;

    // 持久化记录钩子:执行器在用户选 AllowForever 时回调,
    // 入参 = 工具名 + 实际 input。rule(pattern) 由执行器生成(input.dump())。
    using AllowForeverHook = std::function<void(const std::string &tool, const json &input)>;

    // 静默放行判断(命中持久化规则则直接执行,不弹窗)。
    using AllowLookup = std::function<bool(const std::string &tool, const json &input)>;

    // 工具元信息查表(执行器独立于 ToolRegistry,方便测试)。
    using MetaLookup = std::function<ToolMeta(const std::string &tool)>;

    // 可选:在执行每个工具后回调,供 UI 上报。失败/拒绝也会调用。
    using StepReporter = std::function<void(const ToolUseBlock &, const ToolResultBlock &)>;

    // 弹窗间让出:连续两个 asker 调用之间插入的回调(无参)。
    // 默认空(=不延迟)。UI 层可注入:让屏幕重绘一帧、丢弃键盘缓冲、
    // 短暂 sleep,避免 Enter 残留在下一个弹窗上误触。
    using BetweenAsksYield = std::function<void()>;

    // 入队即回调(流式):enqueue 时立刻触发,UI 层可借此提前渲染"即将调工具"
    // 占位卡片。不管后续是否 abort 都会触发;若想在 abort 时不触发,
    // 自己拿 aborted() 状态判断。
    using OnToolCall = std::function<void(const ToolUseBlock &)>;

    void setExecutor(Executor e) { executor_ = std::move(e); }
    void setAsker(Asker a) { asker_ = std::move(a); }
    void setAllowForeverHook(AllowForeverHook h) { allowForeverHook_ = std::move(h); }
    void setAllowLookup(AllowLookup l) { allowLookup_ = std::move(l); }
    void setMetaLookup(MetaLookup m) { metaLookup_ = std::move(m); }
    void setStepReporter(StepReporter r) { stepReporter_ = std::move(r); }
    void setBetweenAsksYield(BetweenAsksYield y) { betweenAsksYield_ = std::move(y); }
    void setOnToolCall(OnToolCall cb) { onToolCall_ = std::move(cb); }

    // ========== 核心方法(纯流式) ==========
    //
    // 使用模式:
    //   for (const auto& tu : toolUses) ic.enqueue(tu);
    //   auto results = ic.run();
    //
    // 流式入队:把单条 ToolUseBlock 推到内部队列,触发 onToolCall_(若设)。
    // 不立即执行 —— 执行由 run() 触发。
    // abort 后再 enqueue 的 tu,run() 时仍会被标 skipped(可观察)。
    void enqueue(ToolUseBlock tu);

    // 触发执行,阻塞到队列耗尽或 abort。
    // 中止语义:任一工具被 Deny 或执行返回 is_error=true,队列立刻终止,
    // 后续所有 ToolUseBlock 直接回写
    //   {id, content="skipped: previous tool was denied/failed: <name>", is_error=true}
    // 不会询问,不会执行。
    std::vector<ToolResultBlock> run();

    // 可读访问(测试 / 调试用)。
    const std::vector<ToolUseBlock>    &queue()   const noexcept { return queue_; }
    const std::vector<ToolResultBlock> &results() const noexcept { return results_; }
    bool aborted() const noexcept { return aborted_; }

  private:
    // 对单条 tu 做 allowLookup/needsPermission/betweenAsksYield/asker/executor 7 步。
    // 返回 std::nullopt 表示该 tu 在中途被中止(Deny),不会推进 queue。
    // 失败 / 拒绝时由 run() 设置 aborted_ + last_failure_name_。
    std::optional<ToolResultBlock> execute_one(const ToolUseBlock &tu);

    std::vector<ToolUseBlock>     queue_;
    std::vector<ToolResultBlock>  results_;
    bool                          aborted_ = false;
    std::string                   last_failure_name_;

    Executor         executor_;
    Asker            asker_;
    AllowForeverHook allowForeverHook_;
    AllowLookup      allowLookup_;        // 可选,默认 = 不命中任何规则
    MetaLookup       metaLookup_;         // 可选,默认 = needsPermission=false
    StepReporter     stepReporter_;       // 可选
    BetweenAsksYield betweenAsksYield_;   // 可选,弹窗间让出
    OnToolCall       onToolCall_;         // 可选,入队即回调(流式)
  };

} // namespace aicoder
