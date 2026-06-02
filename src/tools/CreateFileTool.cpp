#include "tools/CreateFileTool.h"
#include "core/Errors.h"
#include <filesystem>
#include <fstream>

namespace aicoder {

Tool makeCreateFileTool() {
  Tool t;
  t.name = "Create";
  t.description =
      "Create a NEW file with optional content. Fails if the file already "
      "exists (use write_file or edit_file to modify existing files). "
      "Creates parent directories as needed.";
  t.input_schema = json::parse(R"({
      "type": "object",
      "properties": {
        "path": {"type": "string", "description": "Path of the new file"},
        "content": {"type": "string", "description": "Initial content, optional, defaults to empty"}
      },
      "required": ["path"]
  })");
  t.needsPermission = true;
  t.execute = [](const json& input) -> std::string {
    if (!input.contains("path") || !input["path"].is_string())
      throw ToolError("missing path");
    std::string path = input["path"].get<std::string>();
    std::string content = input.value("content", std::string());

    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(path, ec))
      throw ToolError("file already exists: " + path);

    fs::path p(path);
    if (p.has_parent_path() && !p.parent_path().empty())
      fs::create_directories(p.parent_path(), ec);  // 失败时下面 ofstream 会报错

    std::ofstream f(path, std::ios::binary);
    if (!f)
      throw ToolError("cannot create file: " + path);
    f << content;
    if (!f)
      throw ToolError("write failed: " + path);
    return "created: " + path;
  };
  return t;
}

}  // namespace aicoder
