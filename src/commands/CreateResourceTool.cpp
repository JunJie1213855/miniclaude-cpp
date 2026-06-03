#include "commands/CreateResourceTool.h"
#include "commands/CreateCommands.h"
#include "commands/ResourceGenerator.h"
#include "skills/SkillRegistry.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandRouter.h"
#include "subagent/SubAgentRegistry.h"
#include "llm/LlmClient.h"
#include "core/Message.h"
#include "core/Json.h"
#include "core/Errors.h"
#include "workspace/Workspace.h"

#include <filesystem>
#include <string>

namespace aicoder
{
  namespace
  {
    using FactoryFn = std::function<CreateOutcome(const std::string &,
                                                  const std::string &,
                                                  const std::string &,
                                                  const std::filesystem::path &)>;

    // writeAndRegister: invoke the appropriate factory, then register with the
    // matching in-memory registry. onSuccess is invoked with the outcome body
    // (used for rules to append to the system prompt). Returns the success
    // string from the outcome, or throws ToolError on failure.
    std::string writeAndRegister(const std::string &name,
                                 const std::string &description,
                                 const std::string &body,
                                 const std::string &kind,
                                 const std::filesystem::path &dir,
                                 const FactoryFn &factory,
                                 const std::function<void(const std::filesystem::path &)> &registerPath,
                                 const std::function<void(const std::string &)> &onSuccess)
    {
      CreateOutcome r = factory(name, description, body, dir);
      if (!r.error.empty())
        throw ToolError(r.error);
      if (registerPath)
        registerPath(r.path);
      if (onSuccess)
        onSuccess(r.body);
      return r.success;
    }

    // maybeGenerateBody: if userBody is non-empty, return it. Otherwise call
    // the main LLM to synthesize a body for the desired resource kind.
    std::string maybeGenerateBody(LlmClient &client,
                                  const std::string &kind,
                                  const std::string &desiredName,
                                  const std::string &userHint,
                                  const std::string &userBody)
    {
      if (!userBody.empty())
        return userBody;
      GeneratedResource gr = generateResource(client, kind, userHint, desiredName);
      if (gr.body.empty())
        throw ToolError(std::string("[LLM 未返回 ") + kind + " 内容]");
      return gr.body;
    }

    // extract a string from a JSON input, defaulting to "" if missing.
    std::string strField(const json &input, const char *key)
    {
      if (!input.contains(key) || !input[key].is_string())
        return std::string();
      return input[key].get<std::string>();
    }

  } // namespace

  Tool makeCreateSkillTool(LlmClient &client, SkillRegistry &skills)
  {
    Tool t;
    t.name = "create_skill";
    t.description =
        "Create a new skill: write SKILL.md to ~/.aicoder/skills/<name>/ and register it for immediate use. "
        "The body is the instruction text the LLM should follow when this skill is invoked. "
        "If body is empty, the system will auto-generate it from name+description via LLM.";
    t.needsPermission = false;
    t.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"name", json{{"type", "string"}, {"description", "Skill name (lowercase, alnum/_/-)"}}},
            {"description", json{{"type", "string"}, {"description", "One-line description of the skill"}}},
            {"body", json{{"type", "string"}, {"description", "Skill body. If empty, LLM will generate from name+description+context"}}},
            {"context", json{{"type", "string"}, {"description", "Optional extra hint for LLM generation"}}},
        }},
        {"required", json::array({"name"})},
    };

    t.execute = [&client, &skills](const json &input) -> std::string {
      std::string name = strField(input, "name");
      std::string description = strField(input, "description");
      std::string body = strField(input, "body");
      std::string context = strField(input, "context");

      if (name.empty())
        throw ToolError("missing skill name");
      if (!isValidResourceName(name))
        throw ToolError("invalid skill name");

      std::string hint = description;
      if (!context.empty())
      {
        if (!hint.empty())
          hint += " ";
        hint += context;
      }

      body = maybeGenerateBody(client, "skill", name, hint, body);

      FactoryFn factory = [](const std::string &n, const std::string &d,
                             const std::string &b, const std::filesystem::path &p)
      { return createSkill(n, d, b, p); };

      auto registerPath = [&skills](const std::filesystem::path &p)
      { skills.addOne(p); };

      return writeAndRegister(name, description, body, "skill",
                              globalDir() / "skills",
                              factory, registerPath, nullptr);
    };

    return t;
  }

  Tool makeCreateCommandTool(LlmClient &client, CommandRegistry &commands, CommandRouter &router)
  {
    Tool t;
    t.name = "create_command";
    t.description =
        "Create a new slash command: write <name>.md to ~/.aicoder/commands/ and register it for immediate use. "
        "Body is the command template (use $ARGUMENTS for substitution). "
        "If body empty, auto-generate via LLM.";
    t.needsPermission = false;
    t.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"name", json{{"type", "string"}, {"description", "Command name (lowercase, alnum/_/-)"}}},
            {"description", json{{"type", "string"}, {"description", "One-line description of the command"}}},
            {"body", json{{"type", "string"}, {"description", "Command body template. If empty, LLM will generate."}}},
            {"context", json{{"type", "string"}, {"description", "Optional extra hint for LLM generation"}}},
        }},
        {"required", json::array({"name"})},
    };

    t.execute = [&client, &commands, &router](const json &input) -> std::string {
      std::string name = strField(input, "name");
      std::string description = strField(input, "description");
      std::string body = strField(input, "body");
      std::string context = strField(input, "context");

      if (name.empty())
        throw ToolError("missing command name");
      if (!isValidResourceName(name))
        throw ToolError("invalid command name");

      std::string hint = description;
      if (!context.empty())
      {
        if (!hint.empty())
          hint += " ";
        hint += context;
      }

      body = maybeGenerateBody(client, "command", name, hint, body);

      FactoryFn factory = [](const std::string &n, const std::string &d,
                             const std::string &b, const std::filesystem::path &p)
      { return createCommand(n, d, b, p); };

      // Mirror App::Impl::applyCreatePost: addOne(path) for CommandRegistry,
      // then injectCommand into the router so the menu and lookup reflect
      // the new template immediately.
      auto registerPath = [&commands](const std::filesystem::path &p) {
        commands.addOne(p);
      };
      auto onSuccessInject = [&router, name, description, body](const std::string &) {
        router.injectCommand(CommandTemplate{name, description, body});
      };

      return writeAndRegister(name, description, body, "command",
                              globalDir() / "commands",
                              factory, registerPath, onSuccessInject);
    };

    return t;
  }

  Tool makeCreateAgentTool(LlmClient &client, SubAgentRegistry &agents)
  {
    Tool t;
    t.name = "create_agent";
    t.description =
        "Create a new sub-agent: write AGENT.md to ~/.aicoder/agents/<name>/ and register it for immediate use. "
        "Body is the system prompt for the sub-agent. "
        "If body empty, auto-generate via LLM.";
    t.needsPermission = false;
    t.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"name", json{{"type", "string"}, {"description", "Agent name (lowercase, alnum/_/-)"}}},
            {"description", json{{"type", "string"}, {"description", "One-line description of the agent"}}},
            {"body", json{{"type", "string"}, {"description", "Sub-agent system prompt. If empty, LLM will generate."}}},
            {"context", json{{"type", "string"}, {"description", "Optional extra hint for LLM generation"}}},
            {"tools", json{{"type", "array"}, {"items", json{{"type", "string"}}},
                           {"description", "Optional whitelist of tool names the agent may use"}}},
        }},
        {"required", json::array({"name"})},
    };

    t.execute = [&client, &agents](const json &input) -> std::string {
      std::string name = strField(input, "name");
      std::string description = strField(input, "description");
      std::string body = strField(input, "body");
      std::string context = strField(input, "context");

      if (name.empty())
        throw ToolError("missing agent name");
      if (!isValidResourceName(name))
        throw ToolError("invalid agent name");

      std::string hint = description;
      if (!context.empty())
      {
        if (!hint.empty())
          hint += " ";
        hint += context;
      }

      body = maybeGenerateBody(client, "agent", name, hint, body);

      // If the caller specified a tools whitelist, append it to the description
      // so the on-disk file records the constraint (createAgent only takes
      // name/description/body — the tool list is captured in description).
      std::string finalDesc = description;
      if (input.contains("tools") && input["tools"].is_array())
      {
        std::string toolsList;
        for (const auto &t : input["tools"])
        {
          if (!t.is_string())
            continue;
          if (!toolsList.empty())
            toolsList += ",";
          toolsList += t.get<std::string>();
        }
        if (!toolsList.empty())
        {
          if (!finalDesc.empty())
            finalDesc += " ";
          finalDesc += "[tools: " + toolsList + "]";
        }
      }

      FactoryFn factory = [](const std::string &n, const std::string &d,
                             const std::string &b, const std::filesystem::path &p)
      { return createAgent(n, d, b, p); };

      auto registerPath = [&agents](const std::filesystem::path &p)
      { agents.addOne(p); };

      return writeAndRegister(name, finalDesc, body, "agent",
                              globalDir() / "agents",
                              factory, registerPath, nullptr);
    };

    return t;
  }

  Tool makeCreateRuleTool(LlmClient &client,
                          std::function<void(const std::string &)> appendSystemPrompt)
  {
    Tool t;
    t.name = "create_rule";
    t.description =
        "Create a new rule: write to ~/.aicoder/rules/<name>.md and append to the system prompt for immediate effect. "
        "Body is the rule text. "
        "If body empty, auto-generate via LLM.";
    t.needsPermission = false;
    t.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"name", json{{"type", "string"}, {"description", "Rule name (lowercase, alnum/_/-)"}}},
            {"body", json{{"type", "string"}, {"description", "Rule text. If empty, LLM will generate from name+context."}}},
            {"context", json{{"type", "string"}, {"description", "Optional hint describing what the rule is about"}}},
        }},
        {"required", json::array({"name"})},
    };

    t.execute = [&client, &appendSystemPrompt](const json &input) -> std::string {
      std::string name = strField(input, "name");
      std::string body = strField(input, "body");
      std::string context = strField(input, "context");

      if (name.empty())
        throw ToolError("missing rule name");
      if (!isValidResourceName(name))
        throw ToolError("invalid rule name");

      body = maybeGenerateBody(client, "rule", name, context, body);

      FactoryFn factory = [](const std::string &n, const std::string &d,
                             const std::string &b, const std::filesystem::path &p)
      { return createRule(n, d, b, p); };

      // Rules: no path-based registry to addOne to. Instead the system prompt
      // is updated so the rule is in effect immediately for the running session.
      std::function<void(const std::filesystem::path &)> noPathRegister =
          [](const std::filesystem::path &) {};
      std::function<void(const std::string &)> onSuccess =
          [&appendSystemPrompt](const std::string &ruleBody) {
            if (appendSystemPrompt)
              appendSystemPrompt("\n\n" + ruleBody);
          };

      return writeAndRegister(name, "", body, "rule",
                              globalDir() / "rules",
                              factory, noPathRegister, onSuccess);
    };

    return t;
  }

} // namespace aicoder
