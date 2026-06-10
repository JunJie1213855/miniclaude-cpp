#include "core/StreamToolExecutor.h"

#include <utility>

namespace aicoder
{

  namespace
  {
    // 中止后给"被跳过"工具生成的占位 result,文案与原 ToolInterceptor 一致,
    // 现有 7 个测试对该文案强依赖,不得改动。
    ToolResultBlock skipResult(const ToolUseBlock &tu, const std::string &reason)
    {
      return ToolResultBlock{
          tu.id,
          "skipped: previous tool was " + reason,
          true,
      };
    }
  } // namespace

  // ---------- 流式入队 ----------

  void StreamToolExecutor::enqueue(ToolUseBlock tu)
  {
    queue_.push_back(std::move(tu));
    // 入队即回调(若设)。注意:此回调不管后续是否 abort 都会触发,
    // 因为调用方需要"流式显示即将调工具"的语义;若要根据 abort 过滤,
    // 由调用方在回调内判 aborted()。
    if (onToolCall_)
      onToolCall_(queue_.back());
  }

  // ---------- 单轮执行:对单条 tu 做 7 步决策 + 返回 result / 中止信号 ----------
  //
  // 注意:executor_ 若抛异常,沿用原 ToolInterceptor 行为 —— 不在执行器内捕获,
  // 由调用方(ToolRegistry::invoke 已在内部 try/catch 转 is_error=true)兜底。
  // 这样保持与原代码 1:1 的异常传播路径。

  std::optional<ToolResultBlock> StreamToolExecutor::execute_one(const ToolUseBlock &tu)
  {
    ToolMeta meta;
    if (metaLookup_)
      meta = metaLookup_(tu.name);
    else
      meta = ToolMeta{tu.name, false};

    // 1) 静默放行:命中持久化规则
    if (meta.needsPermission && allowLookup_ && allowLookup_(tu.name, tu.input))
    {
      if (executor_)
      {
        return executor_(tu); // 调用方(run)根据 r.is_error 决定是否中止
      }
      // 没 executor 但命中 lookup → 当成功
      return ToolResultBlock{tu.id, "", false};
    }

    // 2) 不需要权限 → 直接执行
    if (!meta.needsPermission)
    {
      if (executor_)
      {
        return executor_(tu);
      }
      // 没 executor(测试 mock)→ 当成功
      return ToolResultBlock{tu.id, "", false};
    }

    // 3) 需要权限且未命中 → 询问
    // 先调用 yield:让 UI 层在两次弹窗间插入缓冲(屏幕重绘、
    // 键盘缓冲清空、短暂 sleep)。这样用户的"上一次 Enter"在
    // 物理上已经抬起/消耗,不会穿透到当前弹窗。
    if (betweenAsksYield_)
      betweenAsksYield_();

    AskResult ask = AskResult::Deny;
    if (asker_)
      ask = asker_(tu, tu.name);
    else
      ask = AskResult::Allow; // 测试默认放行

    if (ask == AskResult::AllowForever && allowForeverHook_)
      allowForeverHook_(tu.name, tu.input);

    if (ask == AskResult::Deny)
    {
      // Deny → 写一个 permission denied result,由 run() 据此设 aborted。
      return ToolResultBlock{tu.id, "permission denied: " + tu.name, true};
    }

    // 4) 允许 → 执行
    if (executor_)
    {
      return executor_(tu);
    }
    return ToolResultBlock{tu.id, "", false};
  }

  // ---------- 多轮执行:跑完整个 queue,处理中止语义 ----------

  std::vector<ToolResultBlock> StreamToolExecutor::run()
  {
    results_.clear();
    results_.reserve(queue_.size());

    for (const auto &tu : queue_)
    {
      if (aborted_)
      {
        // 已中止 → 后续 tu 一律 skipped,文案沿用原 ToolInterceptor。
        ToolResultBlock r = skipResult(tu, last_failure_name_);
        if (stepReporter_)
          stepReporter_(tu, r);
        results_.push_back(std::move(r));
        continue;
      }

      auto opt = execute_one(tu);
      // execute_one 理论上不应返回 nullopt(只走两条 explicit return),
      // 但保留兜底:万一 future 改动引入 nullopt,视为失败中止。
      if (!opt)
      {
        aborted_ = true;
        last_failure_name_ = "failed: " + tu.name;
        ToolResultBlock r = skipResult(tu, last_failure_name_);
        if (stepReporter_)
          stepReporter_(tu, r);
        results_.push_back(std::move(r));
        continue;
      }
      ToolResultBlock r = std::move(*opt);

      // 决定是否中止:
      //   - Deny → content 含 "permission denied" 且 is_error=true
      //   - 执行失败 → is_error=true
      // 二者统一通过 r.is_error 判断,失败原因用 last_failure_name_ 区分。
      // 注意:Deny 的 r.content 形如 "permission denied: Bash",skipped 的
      // 文案是 "skipped: previous tool was denied: Bash",两者语义不同但
      // 都依赖 last_failure_name_ = "denied: " / "failed: " + name。
      if (r.is_error)
      {
        aborted_ = true;
        // 区分 deny vs fail:看 content 是不是 "permission denied: ..." 开头
        if (r.content.rfind("permission denied:", 0) == 0)
          last_failure_name_ = "denied: " + tu.name;
        else
          last_failure_name_ = "failed: " + tu.name;
      }

      if (stepReporter_)
        stepReporter_(tu, r);
      results_.push_back(std::move(r));
    }

    return results_;
  }

} // namespace aicoder