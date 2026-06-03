#include "subagent/SubAgentRegistry.h"
#include "workspace/Workspace.h"
#include "core/Json.h"

namespace aicoder {
namespace fs = std::filesystem;

namespace {

void loadDir(const fs::path& dir, std::map<std::string, SubAgentDef>& out) {
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) return;
	auto mdFiles = listMarkdown(dir);
	for (const auto& p : mdFiles) {
		if (p.filename() != "AGENT.md") continue;
		auto content = readFile(p);
		if (!content) continue;
		Frontmatter fm = parseFrontmatter(*content);
		SubAgentDef def;
		def.name = fm.meta.count("name") ? fm.meta.at("name") : p.parent_path().filename().string();
		def.description = fm.meta.count("description") ? fm.meta.at("description") : "";
		def.role = fm.meta.count("role") ? fm.meta.at("role") : "";
		def.body = fm.body;
		if (fm.meta.count("model")) {
			def.model_override = fm.meta.at("model");
		}
		if (fm.meta.count("max_iterations")) {
			try {
				def.max_iterations = std::stoi(fm.meta.at("max_iterations"));
			} catch (...) {
				// keep default
			}
		}
		if (fm.meta.count("tools")) {
			try {
				json toolsJson = json::parse(fm.meta.at("tools"));
				for (const auto& t : toolsJson) {
					def.tools.push_back(t.get<std::string>());
				}
			} catch (...) {
				// ignore malformed tools
			}
		}
		out[def.name] = std::move(def);
	}
}

} // namespace

void SubAgentRegistry::discover(const fs::path& globalDir, const fs::path& projectDir) {
	agents_.clear();
	loadDir(globalDir, agents_);
	loadDir(projectDir, agents_);
}

void SubAgentRegistry::addOne(const fs::path& agentMdFile) {
	auto content = readFile(agentMdFile);
	if (!content) return;
	Frontmatter fm = parseFrontmatter(*content);
	SubAgentDef def;
	def.name = fm.meta.count("name") ? fm.meta.at("name") : agentMdFile.parent_path().filename().string();
	def.description = fm.meta.count("description") ? fm.meta.at("description") : "";
	def.role = fm.meta.count("role") ? fm.meta.at("role") : "";
	def.body = fm.body;
	if (fm.meta.count("model")) {
		def.model_override = fm.meta.at("model");
	}
	if (fm.meta.count("max_iterations")) {
		try {
			def.max_iterations = std::stoi(fm.meta.at("max_iterations"));
		} catch (...) {
			// keep default
		}
	}
	if (fm.meta.count("tools")) {
		try {
			json toolsJson = json::parse(fm.meta.at("tools"));
			for (const auto& t : toolsJson) {
				def.tools.push_back(t.get<std::string>());
			}
		} catch (...) {
			// ignore malformed tools
		}
	}
	agents_[def.name] = std::move(def);
}

const SubAgentDef* SubAgentRegistry::find(const std::string& name) const {
	auto it = agents_.find(name);
	return it == agents_.end() ? nullptr : &it->second;
}

std::vector<SubAgentDef> SubAgentRegistry::list() const {
	std::vector<SubAgentDef> out;
	for (const auto& [k, v] : agents_) out.push_back(v);
	return out;
}

std::string SubAgentRegistry::promptList() const {
	std::string out;
	for (const auto& [k, v] : agents_) out += "- " + v.name + ": " + v.description + "\n";
	return out;
}

bool SubAgentRegistry::empty() const {
	return agents_.empty();
}

}
