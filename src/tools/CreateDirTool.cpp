#include "tools/CreateDirTool.h"
#include "core/Errors.h"
#include <filesystem>

namespace aicoder {

Tool makeCreateDirTool() {
  Tool t;
  t.name = "CreateDir";
  t.description = "Create a directory, including parent directories as needed.";
  t.input_schema = json::parse(R"({
      "type": "object",
      "properties": {
        "path": {"type": "string", "description": "Directory path to create"}
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
    if (fs::exists(path, ec)) {
      if (fs::is_directory(path, ec))
        return "directory already exists: " + path;
      throw ToolError("path exists but is not a directory: " + path);
    }
    fs::create_directories(path, ec);
    if (ec)
      throw ToolError("cannot create directory: " + path + " (" + ec.message() + ")");
    return "created dir: " + path;
  };
  return t;
}

}  // namespace aicoder
