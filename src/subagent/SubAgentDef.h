#pragma once
#include <string>
#include <vector>
#include <optional>

namespace aicoder {

struct SubAgentDef {
	std::string name;
	std::string description;
	std::string role;
	std::string body;
	std::vector<std::string> tools;
	std::optional<std::string> model_override;
	int max_iterations = 3;
};

}
