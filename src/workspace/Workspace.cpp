#include "workspace/Workspace.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace aicoder
{
  namespace fs = std::filesystem;

  namespace
  {
    std::string trimWs(const std::string &s)
    {
      size_t b = s.find_first_not_of(" \t\r\n");
      if (b == std::string::npos)
        return "";
      size_t e = s.find_last_not_of(" \t\r\n");
      return s.substr(b, e - b + 1);
    }
  } // namespace

  std::optional<std::string> readFile(const fs::path &p)
  {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec))
      return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in)
      return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

  std::vector<fs::path> listMarkdown(const fs::path &dir)
  {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
      return out;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
      if (it->is_regular_file(ec) && it->path().extension() == ".md")
        out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  fs::path globalDir()
  {
    const char *home = std::getenv("HOME");
    if (!home || !*home)
      return {};
    return fs::path(home) / ".aicoder";
  }

  Frontmatter parseFrontmatter(const std::string &content)
  {
    Frontmatter fm;
    if (content.rfind("---\n", 0) != 0)
    {
      fm.body = content;
      return fm;
    }
    size_t end = content.find("\n---", 3);
    if (end == std::string::npos)
    {
      fm.body = content;
      return fm;
    }
    std::string block = content.substr(4, end - 4);
    size_t bodyStart = content.find('\n', end + 1);
    fm.body = (bodyStart == std::string::npos) ? "" : content.substr(bodyStart + 1);
    // Drop the conventional blank line that follows the closing '---' (if any),
    // but preserve any subsequent content verbatim (incl. trailing newline).
    size_t lead = fm.body.find_first_not_of(" \t\r\n");
    if (lead == std::string::npos)
      fm.body.clear();
    else
      fm.body.erase(0, lead);
    std::istringstream is(block);
    std::string line;
    while (std::getline(is, line))
    {
      size_t colon = line.find(':');
      if (colon == std::string::npos)
        continue;
      fm.meta[trimWs(line.substr(0, colon))] = trimWs(line.substr(colon + 1));
    }
    return fm;
  }

} // namespace aicoder
