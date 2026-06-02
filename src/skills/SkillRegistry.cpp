#include "skills/SkillRegistry.h"
#include "workspace/Workspace.h"
namespace aicoder {
namespace fs = std::filesystem;
namespace {
void loadDir(const fs::path& dir, std::map<std::string, SkillInfo>& out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    auto content = readFile(it->path() / "SKILL.md");
    if (!content) continue;
    Frontmatter fm = parseFrontmatter(*content);
    SkillInfo s;
    s.name = fm.meta.count("name") ? fm.meta.at("name") : it->path().filename().string();
    s.description = fm.meta.count("description") ? fm.meta.at("description") : "";
    s.body = fm.body;
    out[s.name] = std::move(s);
  }
}
}
void SkillRegistry::discover(const fs::path& globalDir, const fs::path& projectDir) {
  skills_.clear();
  loadDir(globalDir, skills_);
  loadDir(projectDir, skills_);
}
const SkillInfo* SkillRegistry::find(const std::string& name) const {
  auto it = skills_.find(name);
  return it == skills_.end() ? nullptr : &it->second;
}
std::vector<SkillInfo> SkillRegistry::list() const {
  std::vector<SkillInfo> out;
  for (const auto& [k, v] : skills_) out.push_back(v);
  return out;
}
std::string SkillRegistry::promptList() const {
  std::string out;
  for (const auto& [k, v] : skills_) out += "- " + v.name + ": " + v.description + "\n";
  return out;
}
}
