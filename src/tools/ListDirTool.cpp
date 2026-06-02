#include "tools/ListDirTool.h"
#include "core/Errors.h"
#include <filesystem>

namespace aicoder
{

  Tool makeListDirTool()
  {
    Tool t;
    t.name = "ls";
    t.description = "列出指定目录下的条目（文件 / 子目录）";
    t.input_schema = json{
        {"type", "object"},
        {"properties", {{"path", {{"type", "string"}, {"description", "要列出的目录路径"}}}}},
        {"required", json::array({"path"})}};
    t.execute = [](const json &input) -> std::string
    {
      namespace fs = std::filesystem;
      if (!input.contains("path") || !input["path"].is_string())
        throw ToolError("缺少字符串参数 path");
      std::string path = input["path"].get<std::string>();
      std::error_code ec;
      if (!fs::is_directory(path, ec))
        throw ToolError("不是目录或不存在: " + path);
      std::string out;
      for (const auto &entry : fs::directory_iterator(path, ec))
      {
        out += entry.is_directory() ? "[D] " : "[F] ";
        out += entry.path().filename().string();
        out += "\n";
      }
      return out.empty() ? "(空目录)" : out;
    };
    return t;
  }

}
