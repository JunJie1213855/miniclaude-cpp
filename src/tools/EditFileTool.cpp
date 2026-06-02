#include "tools/EditFileTool.h"
#include "core/Errors.h"
#include <fstream>
#include <sstream>

namespace aicoder {

  Tool makeEditFileTool() {
    Tool t;
    t.name = "Edit";
    t.description = "Edit a file by replacing specific text";
    t.needsPermission = true;  // 文件修改需要权限
    t.input_schema = json::parse(R"({
        "type": "object",
        "properties": {
          "path": {"type": "string", "description": "File path to edit"},
          "old_text": {"type": "string", "description": "Exact text to find and replace"},
          "new_text": {"type": "string", "description": "Replacement text"}
        },
        "required": ["path", "old_text", "new_text"]
    })");
    t.execute = [](const json& input) -> std::string {
      if (!input.contains("path") || !input["path"].is_string())
        throw ToolError("missing path");
      if (!input.contains("old_text") || !input["old_text"].is_string())
        throw ToolError("missing old_text");
      if (!input.contains("new_text") || !input["new_text"].is_string())
        throw ToolError("missing new_text");

      std::string path = input["path"].get<std::string>();
      std::string old_text = input["old_text"].get<std::string>();
      std::string new_text = input["new_text"].get<std::string>();

      // Read entire file
      std::ifstream in(path, std::ios::binary);
      if (!in)
        throw ToolError("cannot open file: " + path);
      std::stringstream ss;
      ss << in.rdbuf();
      std::string content = ss.str();

      // Find and replace (only the first occurrence)
      size_t pos = content.find(old_text);
      if (pos == std::string::npos)
        throw ToolError("old_text not found in file: " + path);

      content.replace(pos, old_text.length(), new_text);

      // Write back
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out)
        throw ToolError("cannot write file: " + path);
      out << content;

      return "edit success: replaced at position " + std::to_string(pos);
    };
    return t;
  }

}