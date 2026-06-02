#include "tools/GlobTool.h"
#include "core/Errors.h"
#include "core/Json.h"
#include <filesystem>
#include <vector>
#include <string>

namespace {
  bool matchPattern(const std::string& pattern, const std::string& str) {
    if (pattern.empty()) return str.empty();
    if (pattern == "*") return true;

    size_t pi = 0, si = 0;
    size_t star = std::string::npos, ssave = std::string::npos;

    while (si < str.size()) {
      if (pi < pattern.size() && (pattern[pi] == '*')) {
        star = pi++;
        ssave = si;
      } else if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si])) {
        pi++;
        si++;
      } else if (star != std::string::npos) {
        pi = star + 1;
        si = ++ssave;
      } else {
        return false;
      }
    }
    while (pi < pattern.size() && pattern[pi] == '*') pi++;
    return pi == pattern.size() && si == str.size();
  }

  void collectGlob(const std::filesystem::path& dir,
                   const std::string& pattern,
                   std::vector<std::string>& out,
                   int& count,
                   int limit) {
    if (count >= limit) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
      if (count >= limit) break;
      std::string name = entry.path().filename().string();
      std::string fullPath = entry.path().string();

      if (matchPattern(pattern, name)) {
        out.push_back(fullPath);
        count++;
      }
      if (entry.is_directory()) {
        collectGlob(entry.path(), pattern, out, count, limit);
      }
    }
  }
}

namespace aicoder {

  Tool makeGlobTool() {
    Tool t;
    t.name = "Glob";
    t.description = "Search for files matching a glob pattern";
    t.input_schema = json::parse(R"({
        "type": "object",
        "properties": {
          "pattern": {"type": "string", "description": "Glob pattern"},
          "path": {"type": "string", "description": "Root directory"}
        },
        "required": ["pattern"]
    })");
    t.execute = [](const json& input) -> std::string {
      std::string pattern = input.value("pattern", "*");
      std::string path = input.value("path", ".");

      namespace fs = std::filesystem;
      std::error_code ec;
      if (!fs::is_directory(path, ec))
        throw ToolError("not a directory: " + path);

      std::vector<std::string> results;
      int count = 0;
      const int limit = 100;
      collectGlob(fs::path(path), pattern, results, count, limit);

      if (results.empty())
        return "(no files matched)";

      std::string out;
      for (const auto& f : results)
        out += f + "\n";
      return out;
    };
    return t;
  }

}