#include "core/ToolRegistry.h"
#include "core/Errors.h"

namespace aicoder {

void ToolRegistry::registerTool(Tool tool) {
  tools_[tool.name] = std::move(tool);
}

bool ToolRegistry::has(const std::string& name) const {
  return tools_.find(name) != tools_.end();
}

bool ToolRegistry::needsPermission(const std::string& name) const {
  auto it = tools_.find(name);
  return it != tools_.end() && it->second.needsPermission;
}

std::vector<ToolSpec> ToolRegistry::specs() const {
  std::vector<ToolSpec> out;
  for (const auto& [name, t] : tools_)
    out.push_back(ToolSpec{t.name, t.description, t.input_schema});
  return out;
}

void ToolRegistry::setPermissionCallback(PermissionCallback cb) {
  permissionCallback_ = std::move(cb);
}

ToolResultBlock ToolRegistry::invoke(const std::string& toolUseId,
                                     const std::string& name,
                                     const json& input) const {
  auto it = tools_.find(name);
  if (it == tools_.end())
    return ToolResultBlock{toolUseId, "unknown tool: " + name, true};

  const Tool& tool = it->second;
  if (tool.needsPermission) {
    bool granted = false;
    if (permissionCallback_)
      granted = permissionCallback_(name, input);
    else
      granted = true;
    if (!granted)
      return ToolResultBlock{toolUseId, "permission denied: " + name, true};
  }

  try {
    std::string out = tool.execute(input);
    return ToolResultBlock{toolUseId, out, false};
  } catch (const ToolError& e) {
    return ToolResultBlock{toolUseId, std::string("tool error: ") + e.what(), true};
  }
}

}