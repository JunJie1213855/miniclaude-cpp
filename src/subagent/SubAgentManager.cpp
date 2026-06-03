#include "subagent/SubAgentManager.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>

#include "core/AgentLoop.h"
#include "core/Message.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"
#include "subagent/SubAgentRegistry.h"
#include "util/ThreadPool.h"

namespace aicoder {

namespace {

constexpr char kTruncationMarker[] = "\n[... output truncated at 100K chars]";

}  // namespace

SubAgentManager::SubAgentManager(LlmClient& client,
                                 const ToolRegistry& fullRegistry,
                                 const SubAgentRegistry& agentReg,
                                 std::function<void(const SubAgentTaskInfo&)> uiNotify,
                                 size_t bgThreads)
    : client_(client),
      fullRegistry_(fullRegistry),
      agentReg_(agentReg),
      runner_(std::make_unique<SubAgentRunner>(client, fullRegistry)),
      ui_notify_(std::move(uiNotify)),
      bg_pool_(std::make_unique<ThreadPool>(bgThreads == 0 ? 1 : bgThreads)) {}

SubAgentManager::~SubAgentManager() {
	shutdown();
	// Joining bg_pool drains in-flight workers so we don't outlive their
	// references to *this; reset() before members destruct.
	bg_pool_.reset();
}

void SubAgentManager::setCancelToken(std::shared_ptr<std::atomic<bool>> cancel) {
	cancel_ = std::move(cancel);
	if (runner_) runner_->setCancelToken(cancel_);
}

void SubAgentManager::setOnToolCall(SubAgentRunner::ToolCallCallback cb) {
	onToolCall_ = std::move(cb);
	if (runner_ && onToolCall_) runner_->setOnToolCall(onToolCall_);
}

void SubAgentManager::setUiNotify(std::function<void(const SubAgentTaskInfo&)> cb) {
	ui_notify_ = std::move(cb);
}

SubAgentResult SubAgentManager::runSync(const SubAgentDef& def,
                                        const std::string& task,
                                        const std::string& context) {
	// Reuse the shared runner; it threads the cancel/onToolCall already set.
	if (runner_) {
		if (cancel_) runner_->setCancelToken(cancel_);
		if (onToolCall_) runner_->setOnToolCall(onToolCall_);
		return runner_->run(def, task, context);
	}
	// Defensive fallback — runner_ should always be present.
	SubAgentRunner local(client_, fullRegistry_);
	if (cancel_) local.setCancelToken(cancel_);
	if (onToolCall_) local.setOnToolCall(onToolCall_);
	return local.run(def, task, context);
}

std::string SubAgentManager::runAsync(const SubAgentDef& def,
                                      const std::string& task,
                                      const std::string& context) {
	auto t = std::make_shared<SubAgentTask>();
	{
		std::lock_guard<std::mutex> lock(table_mu_);
		t->info.task_id = "subagent-" + std::to_string(++next_id_);
		t->info.agent_name = def.name;
		t->info.task = task;
		t->info.status = SubAgentTaskStatus::Pending;
		tasks_[t->info.task_id] = t;
	}

	// Capture-by-value (def + strings) is intentional: the worker runs on a
	// background thread that may outlive the caller's stack frame.
	auto packaged = std::make_shared<std::packaged_task<void()>>(
	    [this, t, def, task, context]() mutable {
		    runWorker(t, std::move(def), std::move(task), std::move(context));
	    });
	t->worker = packaged->get_future().share();

	bg_pool_->submit([packaged]() { (*packaged)(); });
	return t->info.task_id;
}

void SubAgentManager::runWorker(SubAgentTaskPtr task,
                                SubAgentDef def,
                                std::string task_text,
                                std::string context) {
	// Mark Running under the per-task lock so getInfo sees the transition.
	{
		std::lock_guard<std::mutex> lock(task->mu);
		task->info.status = SubAgentTaskStatus::Running;
		task->info.started_at = std::chrono::steady_clock::now();
	}
	if (ui_notify_) {
		SubAgentTaskInfo snapshot;
		{
			std::lock_guard<std::mutex> lock(task->mu);
			snapshot = task->info;
		}
		ui_notify_(snapshot);
	}

	// Fresh per-task SubAgentRunner so cancel/onToolCall state is isolated.
	SubAgentRunner localRunner(client_, fullRegistry_);
	if (cancel_) localRunner.setCancelToken(cancel_);
	if (onToolCall_) localRunner.setOnToolCall(onToolCall_);

	try {
		SubAgentResult result = localRunner.run(def, task_text, context);
		SubAgentTaskStatus final_status =
		    result.was_cancelled || (cancel_ && cancel_->load(std::memory_order_relaxed))
		        ? SubAgentTaskStatus::Cancelled
		        : SubAgentTaskStatus::Completed;
		finalize(std::move(task), final_status, std::move(result.output));
	} catch (const std::exception& e) {
		finalize(std::move(task), SubAgentTaskStatus::Failed, std::string(e.what()));
	} catch (...) {
		finalize(std::move(task), SubAgentTaskStatus::Failed,
		         std::string("unknown exception in sub-agent worker"));
	}
}

void SubAgentManager::finalize(SubAgentTaskPtr task,
                               SubAgentTaskStatus final_status,
                               std::string output_or_error) {
	SubAgentTaskInfo snapshot;
	{
		std::lock_guard<std::mutex> lock(task->mu);
		task->info.status = final_status;
		task->info.finished_at = std::chrono::steady_clock::now();
		if (final_status == SubAgentTaskStatus::Failed) {
			task->info.error = std::move(output_or_error);
		} else {
			task->info.output = truncate(std::move(output_or_error), kSubAgentMaxOutputChars);
		}
		snapshot = task->info;
	}
	task->done_cv.notify_all();
	if (ui_notify_) ui_notify_(snapshot);
}

std::optional<SubAgentTaskInfo> SubAgentManager::getInfo(const std::string& task_id) const {
	SubAgentTaskPtr t = taskPtr(task_id);
	if (!t) return std::nullopt;
	std::lock_guard<std::mutex> lock(t->mu);
	return t->info;
}

SubAgentTaskInfo SubAgentManager::waitFor(const std::string& task_id,
                                          std::chrono::milliseconds timeout) {
	SubAgentTaskPtr t = taskPtr(task_id);
	if (!t) {
		throw std::out_of_range("unknown sub-agent task_id: " + task_id);
	}
	std::unique_lock<std::mutex> lock(t->mu);
	t->done_cv.wait_for(lock, timeout, [&] {
		return t->info.status == SubAgentTaskStatus::Completed ||
		       t->info.status == SubAgentTaskStatus::Failed ||
		       t->info.status == SubAgentTaskStatus::Cancelled;
	});
	return t->info;
}

std::vector<std::string> SubAgentManager::listTasks() const {
	std::vector<std::string> ids;
	std::lock_guard<std::mutex> lock(table_mu_);
	ids.reserve(tasks_.size());
	for (const auto& [id, _] : tasks_) ids.push_back(id);
	return ids;
}

void SubAgentManager::shutdown() {
	if (shutting_down_.exchange(true)) return;
	if (cancel_) cancel_->store(true, std::memory_order_relaxed);

	// Snapshot the task pointers under lock; mutate each task->info under its
	// own per-task mutex so we don't hold two locks at once.
	std::vector<SubAgentTaskPtr> ts;
	{
		std::lock_guard<std::mutex> lock(table_mu_);
		ts.reserve(tasks_.size());
		for (const auto& [_, t] : tasks_) ts.push_back(t);
	}
	for (auto& t : ts) {
		bool changed = false;
		{
			std::lock_guard<std::mutex> lock(t->mu);
			if (t->info.status == SubAgentTaskStatus::Running ||
			    t->info.status == SubAgentTaskStatus::Pending) {
				t->info.status = SubAgentTaskStatus::Cancelled;
				t->info.finished_at = std::chrono::steady_clock::now();
				changed = true;
			}
		}
		if (changed) t->done_cv.notify_all();
	}
}

std::string SubAgentManager::truncate(std::string s, size_t cap) {
	if (s.size() <= cap) return s;
	s.resize(cap);
	s.append(kTruncationMarker);
	return s;
}

SubAgentTaskPtr SubAgentManager::taskPtr(const std::string& task_id) const {
	std::lock_guard<std::mutex> lock(table_mu_);
	auto it = tasks_.find(task_id);
	if (it == tasks_.end()) return nullptr;
	return it->second;
}

}  // namespace aicoder
