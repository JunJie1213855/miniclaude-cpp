#include "subagent/SubAgentTool.h"

#include "core/Errors.h"
#include "subagent/SubAgentDef.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentManager.h"

namespace aicoder {

Tool makeSubAgentTool(const SubAgentRegistry& reg, SubAgentManager& mgr) {
	Tool t;
	t.name = "run_sub_agent";
	t.description =
	    "Spawn a specialized sub-agent to handle a specific task. "
	    "The sub-agent runs with its own system prompt and limited tool set, "
	    "returning its final answer to the parent agent. "
	    "Set run_in_background=true to launch asynchronously and poll later via "
	    "the get_subagent_status / get_subagent_result tools.";
	t.needsPermission = false;

	// Build enum of available agent names.
	json agentSchema = {{"type", "string"},
	                    {"description", "Name of the sub-agent to spawn"}};
	json names = json::array();
	for (const auto& s : reg.list()) {
		names.push_back(s.name);
	}
	if (!names.empty()) {
		agentSchema["enum"] = names;
	}

	t.input_schema = json{
	    {"type", "object"},
	    {"properties",
	     {
	         {"agent", agentSchema},
	         {"task", {{"type", "string"}, {"description", "The task for the sub-agent"}}},
	         {"context",
	          {{"type", "string"},
	           {"description",
	            "Optional context to prefix before the task (e.g. file contents)"}}},
	         {"run_in_background",
	          {{"type", "boolean"},
	           {"default", false},
	           {"description",
	            "If true, launch the sub-agent asynchronously and return a task_id "
	            "immediately; retrieve the answer later with get_subagent_status "
	            "and get_subagent_result. If false (default), block until the "
	            "sub-agent finishes and return its final text."}}},
	     }},
	    {"required", json::array({"agent", "task"})}};

	t.execute = [&reg, &mgr](const json& input) -> std::string {
		if (!input.contains("agent") || !input["agent"].is_string()) {
			throw ToolError("missing agent name");
		}
		std::string agentName = input["agent"].get<std::string>();

		const SubAgentDef* def = reg.find(agentName);
		if (!def) {
			throw ToolError("unknown sub-agent: " + agentName);
		}

		std::string task = input.value("task", std::string(""));
		std::string context = input.value("context", std::string(""));
		bool runInBackground = input.value("run_in_background", false);

		if (runInBackground) {
			std::string id = mgr.runAsync(*def, task, context);
			json reply = {{"status", "async_launched"},
			              {"task_id", id},
			              {"agent", def->name},
			              {"hint",
			               "Use get_subagent_status and get_subagent_result to "
			               "retrieve the answer."}};
			return reply.dump();
		}

		SubAgentResult result = mgr.runSync(*def, task, context);
		return result.output;
	};

	return t;
}

}  // namespace aicoder
