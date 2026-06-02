#include "tools/DeleteFileTool.h"
#include "core/Errors.h"
#include <filesystem>

namespace aicoder {

Tool makeDeleteFileTool() {
  Tool t;
  t.name = "Delete";
  t.description =
      "Delete a single file. Refuses to delete directories (guard against "
      "accidental recursive removal).";
  t.input_schema = json::parse(R"({
      "type": "object",
      "properties": {
        "path": {"type": "string", "description": "File path to delete"}
      },
      "required": ["path"]
  })");
  t.needsPermission = true;
  t.execute = [](const json& input) -> std::string {
    if (!input.contains("path") || !input["path"].is_string())
      throw ToolError("missing path");
    std::string path = input["path"].get<std::string>();

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec))
      throw ToolError("no such file: " + path);
    if (fs::is_directory(path, ec))
      throw ToolError("refusing to delete a directory: " + path);
    if (!fs::remove(path, ec) || ec)
      throw ToolError("delete failed: " + path + " (" + ec.message() + ")");
    return "deleted: " + path;
  };
  return t;
}

}  // namespace aicoder
