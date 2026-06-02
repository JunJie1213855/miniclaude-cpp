#include "tools/ReadFileTool.h"
#include "core/Errors.h"
#include <fstream>
#include <sstream>

namespace aicoder
{

  Tool makeReadFileTool()
  {
    Tool t;
    t.name = "Read";
    t.description = "读取指定路径文件的全部文本内容";
    t.input_schema = json{
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}, {"description", "要读取的文件路径"}}}}},
        {"required", json::array({"path"})}};
    t.execute = [](const json &input) -> std::string
    {
      if (!input.contains("path") || !input["path"].is_string())
        throw ToolError("缺少字符串参数 path");
      std::string path = input["path"].get<std::string>();
      std::ifstream f(path, std::ios::binary);
      if (!f)
        throw ToolError("无法打开文件: " + path);
      std::stringstream ss;
      ss << f.rdbuf();
      return ss.str();
    };
    return t;
  }

}
