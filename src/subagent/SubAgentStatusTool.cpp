#include "subagent/SubAgentStatusTool.h"

#include "core/Errors.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentTask.h"

namespace aicoder {

Tool makeGetSubAgentStatusTool(const SubAgentManager& mgr) {
	Tool t;
	t.name = "get_subagent_status";
	t.description =
	    "Check the current status of a background sub-agent. "
	    "Returns status, agent name, and elapsed time.";
	t.needsPermission = false;

	t.input_schema = json{
	    {"type", "object"},
	    {"properties",
	     {
	         {"task_id",
	          {{"type", "string"},
	           {"description", "The task_id returned by run_sub_agent"}}},
	     }},
	    {"required", json::array({"task_id"})}};

	t.execute = [&mgr](const json& input) -> std::string {
		if (!input.contains("task_id") || !input["task_id"].is_string()) {
			throw ToolError("missing task_id");
		}
		std::string id = input["task_id"].get<std::string>();

		auto info_opt = mgr.getInfo(id);
		if (!info_opt) {
			throw ToolError("unknown task_id: " + id);
		}
		const SubAgentTaskInfo& info = *info_opt;

		// Elapsed milliseconds since the task started. If the task hasn't
		// started yet (started_at default-initialized to epoch), report 0
		// rather than a misleading "running for X hours" number.
		int64_t elapsed_ms = 0;
		if (info.started_at.time_since_epoch().count() != 0) {
			auto now = std::chrono::steady_clock::now();
			elapsed_ms =
			    std::chrono::duration_cast<std::chrono::milliseconds>(now - info.started_at)
			        .count();
			if (elapsed_ms < 0) elapsed_ms = 0;
		}

		bool finished = (info.status != SubAgentTaskStatus::Running &&
		                 info.status != SubAgentTaskStatus::Pending);

		json out = {
		    {"status", to_string(info.status)},
		    {"agent", info.agent_name},
		    {"elapsed_ms", elapsed_ms},
		    {"task_id", info.task_id},
		    {"finished", finished},
		};
		return out.dump();
	};

	return t;
}

}  // namespace aicoder
