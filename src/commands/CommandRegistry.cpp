#include "commands/CommandRegistry.h"
#include "workspace/Workspace.h"
namespace aicoder {
namespace fs = std::filesystem;
namespace {
void loadDir(const fs::path& dir, std::map<std::string, CommandTemplate>& out) {
  for (const auto& md : listMarkdown(dir)) {
    auto content = readFile(md);
    if (!content) continue;
    Frontmatter fm = parseFrontmatter(*content);
    CommandTemplate t;
    t.name = md.stem().string();
    t.description = fm.meta.count("description") ? fm.meta.at("description") : "";
    t.body = fm.body;
    out[t.name] = std::move(t);
  }
}
}
void CommandRegistry::discover(const fs::path& globalDir, const fs::path& projectDir) {
  templates_.clear();
  loadDir(globalDir, templates_);
  loadDir(projectDir, templates_);
}
const CommandTemplate* CommandRegistry::find(const std::string& name) const {
  auto it = templates_.find(name);
  return it == templates_.end() ? nullptr : &it->second;
}
std::vector<CommandTemplate> CommandRegistry::list() const {
  std::vector<CommandTemplate> out;
  for (const auto& [k, v] : templates_) out.push_back(v);
  return out;
}
}
