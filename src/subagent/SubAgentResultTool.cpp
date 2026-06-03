#include "subagent/SubAgentResultTool.h"

#include "core/Errors.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentTask.h"

namespace aicoder {

namespace {

// Clamp timeout into the documented [0, 10min] range. Negative values
// are treated as "wait forever" (0 == non-blocking equivalent inside the
// condition variable wait), and anything past 10 minutes is capped to
// keep a single tool call from monopolizing an agent's turn.
constexpr int kDefaultTimeoutMs = 300000;  // 5 min
constexpr int kMaxTimeoutMs = 600000;      // 10 min

}  // namespace

Tool makeGetSubAgentResultTool(const SubAgentManager& mgr) {
	Tool t;
	t.name = "get_subagent_result";
	t.description =
	    "Retrieve the result of a background sub-agent. "
	    "Blocks until completion (or timeout) by default.";
	t.needsPermission = false;

	t.input_schema = json{
	    {"type", "object"},
	    {"properties",
	     {
	         {"task_id",
	          {{"type", "string"},
	           {"description", "The task_id returned by run_sub_agent"}}},
	         {"wait",
	          {{"type", "boolean"},
	           {"default", true},
	           {"description", "If true, block until the task completes or times out."}}},
	         {"timeout_ms",
	          {{"type", "integer"},
	           {"default", kDefaultTimeoutMs},
	           {"description",
	            "How long to wait (ms) before returning a 'still running' hint. Capped at 10 minutes."}}},
	     }},
	    {"required", json::array({"task_id"})}};

	t.execute = [&mgr](const json& input) -> std::string {
		if (!input.contains("task_id") || !input["task_id"].is_string()) {
			throw ToolError("missing task_id");
		}
		std::string id = input["task_id"].get<std::string>();

		bool wait = input.value("wait", true);
		int timeout = input.value("timeout_ms", kDefaultTimeoutMs);
		if (timeout < 0) timeout = 0;
		if (timeout > kMaxTimeoutMs) timeout = kMaxTimeoutMs;

		auto info_opt = mgr.getInfo(id);
		if (!info_opt) {
			throw ToolError("unknown task_id: " + id);
		}
		SubAgentTaskInfo info = *info_opt;

		if (wait && (info.status == SubAgentTaskStatus::Running ||
		             info.status == SubAgentTaskStatus::Pending)) {
			// waitFor is a non-const method (it installs a condition-variable
			// waiter on the task's control block). The factory contract keeps
			// the manager as `const&` because callers treat this tool as a
			// read-only observer, so we const_cast here — the only mutation is
			// registering a waiter, which is safe and doesn't change any
			// externally observable state.
			info = const_cast<SubAgentManager&>(mgr).waitFor(
			    id, std::chrono::milliseconds(timeout));
		}

		json out = {
		    {"task_id", info.task_id},
		    {"status", to_string(info.status)},
		};

		switch (info.status) {
			case SubAgentTaskStatus::Completed:
				out["output"] = info.output;
				out["iterations_used"] = info.iterations_used;
				break;
			case SubAgentTaskStatus::Cancelled:
				out["error"] = "cancelled: " + info.error;
				break;
			case SubAgentTaskStatus::Failed:
				out["error"] = info.error;
				break;
			case SubAgentTaskStatus::Running:
			case SubAgentTaskStatus::Pending:
				out["hint"] = "still running; call again with wait=true";
				break;
		}

		return out.dump();
	};

	return t;
}

}  // namespace aicoder
