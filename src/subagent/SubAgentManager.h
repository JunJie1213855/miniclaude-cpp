#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/Json.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentRunner.h"
#include "subagent/SubAgentTask.h"

namespace aicoder {

class LlmClient;
class ToolRegistry;
class SubAgentRegistry;
class ThreadPool;

// SubAgentManager owns sync + async execution of sub-agents.
//
// - runSync mirrors the existing SubAgentRunner.run path; suitable when the
//   parent agent wants to block on a result inline.
// - runAsync queues the task to a background pool and returns a task_id
//   immediately, so the orchestrator stays responsive.
// - getInfo / waitFor / listTasks let the UI poll or block on async work.
//
// Cancellation cascades: setCancelToken installs a shared atomic that every
// in-flight and future task observes through its inner AgentLoop.
class SubAgentManager {
public:
	SubAgentManager(LlmClient& client,
	                const ToolRegistry& fullRegistry,
	                const SubAgentRegistry& agentReg,
	                std::function<void(const SubAgentTaskInfo&)> uiNotify = {},
	                size_t bgThreads = 2);
	~SubAgentManager();

	// Non-copyable / non-movable: holds threads and mutexes.
	SubAgentManager(const SubAgentManager&) = delete;
	SubAgentManager& operator=(const SubAgentManager&) = delete;

	void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel);
	void setOnToolCall(SubAgentRunner::ToolCallCallback cb);
	void setUiNotify(std::function<void(const SubAgentTaskInfo&)> cb);

	// Inline (blocking) execution. Returns whatever the sub-agent produced.
	SubAgentResult runSync(const SubAgentDef& def,
	                       const std::string& task,
	                       const std::string& context = "");

	// Background execution. Returns the task_id immediately; caller polls via
	// getInfo or blocks via waitFor.
	std::string runAsync(const SubAgentDef& def,
	                     const std::string& task,
	                     const std::string& context = "");

	// Snapshot lookup; nullopt for unknown task_id.
	std::optional<SubAgentTaskInfo> getInfo(const std::string& task_id) const;

	// Wait up to `timeout` for the task to reach a terminal status, then
	// return its current info. Throws std::out_of_range for unknown task_id.
	SubAgentTaskInfo waitFor(const std::string& task_id,
	                         std::chrono::milliseconds timeout);

	std::vector<std::string> listTasks() const;

	// Trip the shared cancel token (if set) and mark any still-running tasks
	// Cancelled. Worker threads drain via ThreadPool's destructor.
	void shutdown();

private:
	void runWorker(SubAgentTaskPtr task,
	               SubAgentDef def,
	               std::string task_text,
	               std::string context);

	void finalize(SubAgentTaskPtr task,
	              SubAgentTaskStatus final_status,
	              std::string output_or_error);

	static std::string truncate(std::string s, size_t cap);

	SubAgentTaskPtr taskPtr(const std::string& task_id) const;

	LlmClient& client_;
	const ToolRegistry& fullRegistry_;
	const SubAgentRegistry& agentReg_;
	std::unique_ptr<SubAgentRunner> runner_;  // shared for sync path
	std::function<void(const SubAgentTaskInfo&)> ui_notify_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	SubAgentRunner::ToolCallCallback onToolCall_;
	std::unique_ptr<ThreadPool> bg_pool_;
	mutable std::mutex table_mu_;
	std::map<std::string, SubAgentTaskPtr> tasks_;
	std::atomic<uint64_t> next_id_{0};
	std::atomic<bool> shutting_down_{false};
};

}  // namespace aicoder
