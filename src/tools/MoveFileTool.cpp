#include "tools/MoveFileTool.h"
#include "core/Errors.h"
#include <filesystem>

namespace aicoder {

Tool makeMoveFileTool() {
  Tool t;
  t.name = "Move";
  t.description = "Move or rename a file or directory (source -> dest).";
  t.input_schema = json::parse(R"({
      "type": "object",
      "properties": {
        "source": {"type": "string", "description": "Existing path to move"},
        "dest": {"type": "string", "description": "Destination path"}
      },
      "required": ["source", "dest"]
  })");
  t.needsPermission = true;
  t.execute = [](const json& input) -> std::string {
    if (!input.contains("source") || !input["source"].is_string())
      throw ToolError("missing source");
    if (!input.contains("dest") || !input["dest"].is_string())
      throw ToolError("missing dest");
    std::string source = input["source"].get<std::string>();
    std::string dest = input["dest"].get<std::string>();

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(source, ec))
      throw ToolError("no such source: " + source);
    fs::rename(source, dest, ec);
    if (ec)
      throw ToolError("move failed: " + source + " -> " + dest + " (" + ec.message() + ")");
    return "moved: " + source + " -> " + dest;
  };
  return t;
}

}  // namespace aicoder
