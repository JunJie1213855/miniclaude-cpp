#include <filesystem>
#include <iostream>
#include "commands/CommandRegistry.h"
#include "commands/CommandRouter.h"
#include "commands/CreateResourceTool.h"
#include "config/Config.h"
#include "core/AgentLoop.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "llm/DefaultLlmClient.h"
#include "llm/HttpTransport.h"
#include "llm/OpenAIProvider.h"
#include "rules/Rules.h"
#include "sessions/CliArgs.h"
#include "sessions/SessionStore.h"
#include "skills/SkillRegistry.h"
#include "skills/SkillTool.h"
#include "tools/BuiltinTools.h"
#include "ui/App.h"
#include "ui/ResumePicker.h"
#include "subagent/SubAgentManager.h"
#include "mcp/McpManager.h"
#include "mcp/McpTypes.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentResultTool.h"
#include "subagent/SubAgentStatusTool.h"
#include "subagent/SubAgentTool.h"
#include "workspace/Workspace.h"

using namespace aicoder;

int main(int argc, char** argv) {
  auto args = parseCliArgs(argc, argv);
  if (!args) {
    std::cerr << kUsage;
    return 2;
  }

  // --list-sessions：不进 TUI、不需要 API key，列完即退。删除请手动 rm -rf。
  if (args->mode == CliMode::ListSessions) {
    SessionStore store(globalDir() / "sessions");
    auto sessions = store.listSessions();
    if (sessions.empty()) {
      std::cout << "(no sessions in " << store.root().string() << ")\n";
    } else {
      std::cout << "Sessions in " << store.root().string() << ":\n";
      for (const auto& s : sessions) {
        std::cout << "  " << s.id << "  " << s.updated_at
                  << "  [" << s.message_count << " 条]  "
                  << (s.preview.empty() ? "(无预览)" : s.preview) << "\n";
      }
      std::cout << "\nDelete with: rm -rf " << store.root().string() << "/<id>\n";
    }
    return 0;
  }

  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }

  ToolRegistry registry;
  registerBuiltinTools(registry);

  static SkillRegistry skillReg;  // static：必须比 skill 工具的引用捕获活得久
  skillReg.discover(globalDir() / "skills",
                    std::filesystem::current_path() / ".aicoder" / "skills");
  registry.registerTool(makeSkillTool(skillReg));

  static SubAgentRegistry agentReg;
  agentReg.discover(globalDir() / "agents",
                    std::filesystem::current_path() / ".aicoder" / "agents");

  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  AgentLoop loop(client, registry, config.max_iterations);
  static SubAgentManager manager(client, registry, agentReg);
  if (!agentReg.empty()) {
    registry.registerTool(makeSubAgentTool(agentReg, manager));
    registry.registerTool(makeGetSubAgentStatusTool(manager));
    registry.registerTool(makeGetSubAgentResultTool(manager));
  }

  // MCP: connect to configured servers and register their tools
  std::vector<aicoder::mcp::McpServerConfig> mcpConfigs;
  for (const auto& [name, raw] : config.mcp_servers) {
    mcpConfigs.push_back({name, raw.command, raw.args, raw.env, raw.description});
  }
  static aicoder::mcp::McpManager mcpManager(std::move(mcpConfigs));
  if (!config.mcp_servers.empty()) {
    int n = mcpManager.registerAllTools(registry);
    fprintf(stderr, "[MCP] registered %d tools from %zu servers\n",
            n, config.mcp_servers.size());
  }

  CommandRegistry commandReg;
  commandReg.discover(globalDir() / "commands",
                      std::filesystem::current_path() / ".aicoder" / "commands");
  CommandRouter router(std::move(commandReg), &skillReg,
                       globalDir() / "skills",
                       std::filesystem::current_path() / ".aicoder" / "skills");

  // 注册 4 个 create_* 工具: LLM 在对话中可主动调用
  // 注意: 注册时机在 App 创建之前,所以 create_rule 的 system prompt 追加回调
  // 只写入文件,系统 prompt 集成留给后续 session(v1 接受的折衷)。
  registry.registerTool(makeCreateSkillTool(client, skillReg));
  registry.registerTool(makeCreateCommandTool(client, commandReg, router));
  registry.registerTool(makeCreateAgentTool(client, agentReg));
  registry.registerTool(makeCreateRuleTool(client, [](const std::string&) {
    // 规则文件已由工厂写入,系统 prompt 集成留待后续 session 生效。
  }));

  std::string systemPrompt = buildSystemPrompt(
      kBaseSystemPrompt,
      loadRules(globalDir(), std::filesystem::current_path()),
      skillReg.promptList());
  if (!agentReg.empty()) {
    systemPrompt += "\n\n## 可用子代理\n" + agentReg.promptList();
    systemPrompt += "\n\n## 子代理后台运行\n- run_sub_agent 支持 run_in_background: true，立即返回 task_id。\n- get_subagent_status(task_id) 查询状态。\n- get_subagent_result(task_id, wait=true) 阻塞取回结果。\n";
  }
  systemPrompt += "\n\n## 资源创建工具\nYou can proactively call create_skill, create_command, create_agent, create_rule during a conversation when you recognize a need for a reusable resource. If body is empty, the system auto-generates content via LLM.\n";

  if (!config.mcp_servers.empty()) {
    systemPrompt += "\n\n## MCP 工具\n外部 MCP 工具（通过 mcp__<server>__<tool> 调用）:\n";
    for (const auto& [name, cfg] : config.mcp_servers) {
      systemPrompt += "- " + name + ": " + cfg.description + "\n";
    }
  }

  SessionStore sessionStore(globalDir() / "sessions");
  std::string sessionId;
  std::vector<Message> initialMessages;

  if (args->mode == CliMode::ContinueLatest) {
    if (auto id = sessionStore.latestId()) {
      sessionId = *id;
      if (auto data = sessionStore.load(*id)) initialMessages = std::move(data->messages);
      else { std::cerr << "[会话已损坏，开新会话]\n"; sessionId = sessionStore.newId(); }
    } else {
      std::cerr << "[未找到历史会话，开始新会话]\n";
      sessionId = sessionStore.newId();
    }
  } else if (args->mode == CliMode::ResumePicker) {
    auto picked = showResumePicker(sessionStore);
    if (picked) {
      sessionId = *picked;
      if (auto data = sessionStore.load(*picked)) initialMessages = std::move(data->messages);
      else { std::cerr << "[会话已损坏，开新会话]\n"; sessionId = sessionStore.newId(); }
    } else {
      sessionId = sessionStore.newId();
    }
  } else {
    sessionId = sessionStore.newId();
  }

  App app(loop, router, systemPrompt, sessionStore, sessionId,
          std::move(initialMessages), config.model);
  app.setSubAgentManager(&manager);
  app.setSubAgentRegistry(&agentReg);
  // MCP cancel token propagation is handled via SubAgentManager sharing
  app.setSkillRegistry(&skillReg);
  app.run();
  return 0;
}