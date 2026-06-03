#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "subagent/SubAgentDef.h"

namespace aicoder {

class SubAgentRegistry {
public:
	void discover(const std::filesystem::path& globalDir,
	              const std::filesystem::path& projectDir);
	void addOne(const std::filesystem::path& agentMdFile);
	const SubAgentDef* find(const std::string& name) const;
	std::vector<SubAgentDef> list() const;
	std::string promptList() const;
	bool empty() const;

private:
	std::map<std::string, SubAgentDef> agents_;
};

}
