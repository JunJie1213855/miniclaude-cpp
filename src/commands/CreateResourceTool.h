#pragma once
#include "core/Tool.h"
#include "core/Json.h"
namespace aicoder {
class LlmClient;
class SkillRegistry;
class SubAgentRegistry;
class CommandRegistry;
class CommandRouter;
// Each factory takes refs to the in-memory registries and the LLM client.
// After creating the file, registers with the corresponding registry for immediate use.
Tool makeCreateSkillTool(LlmClient& client, SkillRegistry& skills);
Tool makeCreateCommandTool(LlmClient& client, CommandRegistry& commands, CommandRouter& router);
Tool makeCreateAgentTool(LlmClient& client, SubAgentRegistry& agents);
Tool makeCreateRuleTool(LlmClient& client, std::function<void(const std::string&)> appendSystemPrompt);
}
