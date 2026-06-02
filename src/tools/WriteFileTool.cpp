#include "tools/WriteFileTool.h"
#include "core/Errors.h"
#include <fstream>

namespace aicoder
{

  Tool makeWriteFileTool()
  {
    Tool t;
    t.name = "Write";
    t.description = "Write text content to a file";
    t.input_schema = json::parse(R"({
        "type": "object",
        "properties": {
          "path": {"type": "string", "description": "Target file path"},
          "content": {"type": "string", "description": "Text content to write"}
        },
        "required": ["path", "content"]
    })");
    t.needsPermission = true;
    t.execute = [](const json &input) -> std::string
    {
      if (!input.contains("path") || !input["path"].is_string())
        throw ToolError("missing path");
      if (!input.contains("content") || !input["content"].is_string())
        throw ToolError("missing content");
      std::string path = input["path"].get<std::string>();
      std::string content = input["content"].get<std::string>();
      std::ofstream f(path, std::ios::binary);
      if (!f)
        throw ToolError("cannot open file: " + path);
      f << content;
      if (!f)
        throw ToolError("write failed: " + path);
      return "write success: " + path;
    };
    return t;
  }

}
