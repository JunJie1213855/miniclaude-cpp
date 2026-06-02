#pragma once
#include <functional>
#include <string>
#include "core/Json.h"

namespace aicoder
{

  // 完整工具：带执行回调。execute 成功返回结果文本；失败抛 ToolError。
  // needsPermission=true 时，执行前会调 permissionCallback 询问用户。
  struct Tool
  {
    std::string name;
    std::string description;
    json input_schema;
    std::function<std::string(const json &)> execute;
    bool needsPermission = false;
  };

  // 工具规格：不含回调，提供给 Provider 编码进请求的 tools 字段。
  struct ToolSpec
  {
    std::string name;
    std::string description;
    json input_schema;
  };

}
