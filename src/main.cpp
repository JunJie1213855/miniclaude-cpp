#include <filesystem>
#include <iostream>
#include <memory>
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
#include "skills/SkillRegistry.h"
#include "skills/SkillTool.h"
#include "tools/BuiltinTools.h"
#include "ui/ConsoleRepl.h"
#include "workspace/Workspace.h"

using namespace aicoder;

int main() {
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

  ConsoleRepl repl(loop, router, systemPrompt);
  repl.run();
  return 0;
}