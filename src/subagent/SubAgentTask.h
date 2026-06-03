#pragma once

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace aicoder {

// Cap output captured per task. Beyond this we append a truncation marker to
// keep the parent agent's context bounded and predictable.
inline constexpr size_t kSubAgentMaxOutputChars = 100000;

enum class SubAgentTaskStatus {
	Pending,
	Running,
	Completed,
	Failed,
	Cancelled,
};

// Snapshot-friendly value type. SubAgentManager copies this out under lock
// so callers never observe a partially-mutated state.
struct SubAgentTaskInfo {
	std::string task_id;
	std::string agent_name;
	std::string task;
	SubAgentTaskStatus status = SubAgentTaskStatus::Pending;
	std::string output;
	std::string error;
	std::chrono::steady_clock::time_point started_at{};
	std::chrono::steady_clock::time_point finished_at{};
	int iterations_used = 0;
};

// Per-task control block. `mu` guards `info`; `done_cv` notifies waiters when
// the task transitions to a terminal status. `worker` is a shared_future so
// multiple callers may wait on it without consuming the result.
struct SubAgentTask {
	SubAgentTaskInfo info;
	std::shared_future<void> worker;
	mutable std::mutex mu;
	std::condition_variable done_cv;
};

using SubAgentTaskPtr = std::shared_ptr<SubAgentTask>;

inline std::string to_string(SubAgentTaskStatus s) {
	switch (s) {
		case SubAgentTaskStatus::Pending:
			return "pending";
		case SubAgentTaskStatus::Running:
			return "running";
		case SubAgentTaskStatus::Completed:
			return "completed";
		case SubAgentTaskStatus::Failed:
			return "failed";
		case SubAgentTaskStatus::Cancelled:
			return "cancelled";
	}
	return "unknown";
}

}  // namespace aicoder
