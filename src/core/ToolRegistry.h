#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/Tool.h"
#include "core/Message.h"

namespace aicoder
{

  class ToolRegistry
  {
  public:
    using PermissionCallback = std::function<bool(const std::string &toolName, const json &input)>;

    void registerTool(Tool tool);
    bool has(const std::string &name) const;
    // 该工具是否声明执行前需用户确认（needsPermission）。未知工具返回 false。
    bool needsPermission(const std::string &name) const;
    std::vector<ToolSpec> specs() const;
    // 设置权限询问回调；若未设置，默认返回 true（自动允许）。
    void setPermissionCallback(PermissionCallback cb);
    // 执行工具；捕获 ToolError / 未知工具 → 返回 is_error=true 的 result。
    // 若工具 needsPermission=true，先调 permissionCallback，权限拒绝则返回 is_error=true。
    ToolResultBlock invoke(const std::string &toolUseId,
                           const std::string &name,
                           const json &input) const;

    ToolRegistry filter(const std::vector<std::string>& names) const;

  private:
    std::map<std::string, Tool> tools_;
    PermissionCallback permissionCallback_;
  };

}
