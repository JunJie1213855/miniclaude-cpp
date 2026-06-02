#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"

namespace aicoder
{

  // UI 给拦截器的"询问一次"回调。返回 Allow / AllowForever / Deny。
  enum class AskResult
  {
    Allow,
    AllowForever,
    Deny
  };

  // 一个工具的元信息(让拦截器能决定"是否需要询问",而不直接 import ToolRegistry)。
  struct ToolMeta
  {
    std::string name;
    bool needsPermission = false;
  };

  // 拦截器配置:每个回调都能注入(便于测试替换)。
  class ToolInterceptor
  {
  public:
    // 真正执行一个工具:返回 ToolResultBlock(已含异常转 is_error=true)。
    using Executor = std::function<ToolResultBlock(const ToolUseBlock &)>;

    // 弹窗询问:返回 Allow / AllowForever / Deny;若为 AllowForever,
    // 拦截器会调 onAllowForever 把规则写进持久化。
    using Asker = std::function<AskResult(const ToolUseBlock &, const std::string &toolName)>;

    // 持久化记录钩子:拦截器在用户选 AllowForever 时回调,
    // 入参 = 工具名 + 实际 input。rule(pattern) 由拦截器生成(input.dump())。
    using AllowForeverHook = std::function<void(const std::string &tool, const json &input)>;

    // 静默放行判断(命中持久化规则则直接执行,不弹窗)。
    using AllowLookup = std::function<bool(const std::string &tool, const json &input)>;

    // 工具元信息查表(拦截器独立于 ToolRegistry,方便测试)。
    using MetaLookup = std::function<ToolMeta(const std::string &tool)>;

    // 可选:在执行每个工具后回调,供 UI 上报。失败/拒绝也会调用。
    using StepReporter = std::function<void(const ToolUseBlock &, const ToolResultBlock &)>;

    void setExecutor(Executor e) { executor_ = std::move(e); }
    void setAsker(Asker a) { asker_ = std::move(a); }
    void setAllowForeverHook(AllowForeverHook h) { allowForeverHook_ = std::move(h); }
    void setAllowLookup(AllowLookup l) { allowLookup_ = std::move(l); }
    void setMetaLookup(MetaLookup m) { metaLookup_ = std::move(m); }
    void setStepReporter(StepReporter r) { stepReporter_ = std::move(r); }

    // 顺序处理 LLM 一次性返回的 N 个 ToolUseBlock。
    // 中断逻辑:任一工具被 Deny(用户拒绝)或执行返回 is_error=true,队列立刻终止,
    // 后续所有 ToolUseBlock 直接回写
    //   {id, content="skipped: previous tool was denied/failed", is_error=true}
    // 不会询问,不会执行。
    std::vector<ToolResultBlock> intercept(const std::vector<ToolUseBlock> &queue);

  private:
    Executor executor_;
    Asker asker_;
    AllowForeverHook allowForeverHook_;
    AllowLookup allowLookup_;   // 可选,默认 = 不命中任何规则
    MetaLookup metaLookup_;     // 可选,默认 = needsPermission=false
    StepReporter stepReporter_; // 可选
  };

} // namespace aicoder
