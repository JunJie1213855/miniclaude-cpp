#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "core/Json.h"
#include "subagent/SubAgentDef.h"

namespace aicoder {

class AgentLoop;
class ToolRegistry;
class LlmClient;

struct SubAgentResult {
	std::string output;
	bool was_cancelled = false;
};

class SubAgentRunner {
public:
	SubAgentRunner(LlmClient& client, const ToolRegistry& fullRegistry);

	void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel);

	using ToolCallCallback = std::function<void(const std::string&, const json&,
	                                            const std::string&, bool)>;
	void setOnToolCall(ToolCallCallback cb);

	SubAgentResult run(const SubAgentDef& def, const std::string& task,
	                   const std::string& context = "");

private:
	LlmClient& client_;
	const ToolRegistry& fullRegistry_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	ToolCallCallback onToolCall_;
};

}  // namespace aicoder
