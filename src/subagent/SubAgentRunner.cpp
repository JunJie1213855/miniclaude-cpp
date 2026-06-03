#include "subagent/SubAgentRunner.h"

#include "core/AgentLoop.h"
#include "core/Message.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"

namespace aicoder {

SubAgentRunner::SubAgentRunner(LlmClient& client, const ToolRegistry& fullRegistry)
    : client_(client), fullRegistry_(fullRegistry) {}

void SubAgentRunner::setCancelToken(std::shared_ptr<std::atomic<bool>> cancel) {
	cancel_ = std::move(cancel);
}

void SubAgentRunner::setOnToolCall(ToolCallCallback cb) {
	onToolCall_ = std::move(cb);
}

SubAgentResult SubAgentRunner::run(const SubAgentDef& def,
                                   const std::string& task,
                                   const std::string& context) {
	// 1) Filter tool registry to only the tools this sub-agent is allowed to use.
	ToolRegistry filteredRegistry = fullRegistry_.filter(def.tools);

	// 2) Create the sub-agent loop.
	AgentLoop subLoop(client_, filteredRegistry, def.max_iterations);

	// 3) Propagate cancel token.
	subLoop.setCancelToken(cancel_);

	// 4) Propagate tool-call observer.
	if (onToolCall_) {
		subLoop.setOnToolCall(onToolCall_);
	}

	// 5) Build the initial message list.
	std::vector<Message> messages;
	messages.push_back(systemText(def.role));

	if (!context.empty()) {
		messages.push_back(userText("Context:\n" + context));
	}

	messages.push_back(userText("Task: " + task));

	// 6) Run the loop.
	std::string output = subLoop.run(messages);

	// 7) Return result.
	return SubAgentResult{std::move(output), subLoop.cancelRequested()};
}

}  // namespace aicoder
