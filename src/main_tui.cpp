#include <filesystem>
#include <iostream>
#include "commands/CommandRegistry.h"
#include "commands/CommandRouter.h"
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

  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  AgentLoop loop(client, registry, config.max_iterations);
  CommandRegistry commandReg;
  commandReg.discover(globalDir() / "commands",
                      std::filesystem::current_path() / ".aicoder" / "commands");
  CommandRouter router(std::move(commandReg), &skillReg,
                       globalDir() / "skills",
                       std::filesystem::current_path() / ".aicoder" / "skills");

  std::string systemPrompt = buildSystemPrompt(
      kBaseSystemPrompt,
      loadRules(globalDir(), std::filesystem::current_path()),
      skillReg.promptList());

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
  app.run();
  return 0;
}