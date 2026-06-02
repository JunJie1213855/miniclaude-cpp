#include "tools/GrepTool.h"
#include "core/Errors.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace {
  // Check if file should be skipped (binary files)
  bool isBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[8192];
    f.read(buf, sizeof(buf));
    std::streamsize n = f.gcount();
    for (std::streamsize i = 0; i < n; i++) {
      unsigned char c = static_cast<unsigned char>(buf[i]);
      if (c == 0) return true;  // null byte found = binary
    }
    return false;
  }

  void searchInFile(const std::filesystem::path& filePath,
                   const std::string& pattern,
                   std::vector<std::string>& out,
                   int& matchCount,
                   int maxMatches,
                   bool isRegex) {
    if (matchCount >= maxMatches) return;
    if (isBinaryFile(filePath.string())) return;

    std::ifstream f(filePath.string(), std::ios::binary);
    if (!f) return;

    std::string line;
    int lineNum = 0;
    while (std::getline(f, line)) {
      lineNum++;
      bool found = false;
      if (isRegex) {
        // Simple regex check: just check if pattern appears as substring for now
        // A full regex implementation would need <regex>
        found = line.find(pattern) != std::string::npos;
      } else {
        found = line.find(pattern) != std::string::npos;
      }

      if (found) {
        // Truncate long lines
        std::string display = line;
        if (display.size() > 200)
          display = display.substr(0, 200) + "...";
        out.push_back(filePath.string() + ":" + std::to_string(lineNum) + ": " + display);
        matchCount++;
        if (matchCount >= maxMatches) break;
      }
    }
  }

  void collectGrep(const std::filesystem::path& dir,
                   const std::string& pattern,
                   std::vector<std::string>& out,
                   int& matchCount,
                   int maxMatches,
                   bool isRegex) {
    if (matchCount >= maxMatches) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
      if (matchCount >= maxMatches) break;
      if (entry.is_directory()) {
        // Skip hidden directories and common non-source dirs
        std::string name = entry.path().filename().string();
        if (name == ".git" || name == "node_modules" || name == "build" || name == "_deps")
          continue;
        collectGrep(entry.path(), pattern, out, matchCount, maxMatches, isRegex);
      } else if (entry.is_regular_file()) {
        searchInFile(entry.path(), pattern, out, matchCount, maxMatches, isRegex);
      }
    }
  }
}

namespace aicoder {

  Tool makeGrepTool() {
    Tool t;
    t.name = "Grep";
    t.description = "Search for text pattern in files";
    t.input_schema = json::parse(R"({
        "type": "object",
        "properties": {
          "pattern": {"type": "string", "description": "Text pattern or regex to search for"},
          "path": {"type": "string", "description": "Root directory"},
          "is_regex": {"type": "boolean", "description": "Treat as regex"}
        },
        "required": ["pattern"]
    })");
    t.execute = [](const json& input) -> std::string {
      std::string pattern = input.value("pattern", "");
      std::string path = input.value("path", ".");
      bool isRegex = input.value("is_regex", false);

      if (pattern.empty())
        throw ToolError("pattern cannot be empty");

      namespace fs = std::filesystem;
      std::error_code ec;
      if (!fs::is_directory(path, ec))
        throw ToolError("not a directory: " + path);

      std::vector<std::string> results;
      int matchCount = 0;
      const int maxMatches = 100;
      collectGrep(fs::path(path), pattern, results, matchCount, maxMatches, isRegex);

      if (results.empty())
        return "(no matches found)";

      std::string out;
      for (const auto& r : results)
        out += r + "\n";
      out += "\n(" + std::to_string(matchCount) + " matches)";
      return out;
    };
    return t;
  }

}