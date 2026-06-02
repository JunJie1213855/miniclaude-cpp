#include "core/ToolInterceptor.h"

#include <utility>

namespace aicoder
{

  namespace
  {
    ToolResultBlock skipResult(const ToolUseBlock &tu, const std::string &reason)
    {
      return ToolResultBlock{
          tu.id,
          "skipped: previous tool was " + reason,
          true,
      };
    }
  } // namespace

  std::vector<ToolResultBlock> ToolInterceptor::intercept(const std::vector<ToolUseBlock> &queue)
  {
    std::vector<ToolResultBlock> out;
    out.reserve(queue.size());

    bool aborted = false; // 任一工具被 Deny 或执行失败 → 后续一律 skipped
    std::string abortReason;

    for (const auto &tu : queue)
    {
      if (aborted)
      {
        ToolResultBlock r = skipResult(tu, abortReason);
        if (stepReporter_)
          stepReporter_(tu, r);
        out.push_back(std::move(r));
        continue;
      }

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
          ToolResultBlock r = executor_(tu);
          if (r.is_error)
          {
            aborted = true;
            abortReason = "failed: " + tu.name;
          }
          if (stepReporter_)
            stepReporter_(tu, r);
          out.push_back(std::move(r));
          continue;
        }
      }

      // 2) 不需要权限 → 直接执行
      if (!meta.needsPermission)
      {
        if (executor_)
        {
          ToolResultBlock r = executor_(tu);
          if (r.is_error)
          {
            aborted = true;
            abortReason = "failed: " + tu.name;
          }
          if (stepReporter_)
            stepReporter_(tu, r);
          out.push_back(std::move(r));
          continue;
        }
        // 没 executor 就当成功(测试 mock 场景)。
        out.push_back(ToolResultBlock{tu.id, "", false});
        continue;
      }

      // 3) 需要权限且未命中 → 询问
      AskResult ask = AskResult::Deny;
      if (asker_)
        ask = asker_(tu, tu.name);
      else
        ask = AskResult::Allow; // 测试默认放行

      if (ask == AskResult::AllowForever && allowForeverHook_)
        allowForeverHook_(tu.name, tu.input);

      if (ask == AskResult::Deny)
      {
        ToolResultBlock r{tu.id, "permission denied: " + tu.name, true};
        if (stepReporter_)
          stepReporter_(tu, r);
        out.push_back(std::move(r));
        aborted = true;
        abortReason = "denied: " + tu.name;
        continue;
      }

      // 4) 允许 → 执行
      if (executor_)
      {
        ToolResultBlock r = executor_(tu);
        if (r.is_error)
        {
          aborted = true;
          abortReason = "failed: " + tu.name;
        }
        if (stepReporter_)
          stepReporter_(tu, r);
        out.push_back(std::move(r));
        continue;
      }
      out.push_back(ToolResultBlock{tu.id, "", false});
    }

    return out;
  }

} // namespace aicoder
