#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ftxui/component/component.hpp"
#include "core/Message.h"

namespace aicoder {
class AgentLoop;
class CommandRouter;
class ReplView;
class SessionStore;

class App {
public:
  App(AgentLoop& loop, CommandRouter& router, const std::string& systemPrompt,
      SessionStore& store, std::string initialSessionId,
      std::vector<Message> initialMessages, std::string model);
  ~App();
  void run();
  void setSubAgentManager(class SubAgentManager* mgr);
  void setSubAgentRegistry(class SubAgentRegistry* reg);
  void setSkillRegistry(class SkillRegistry* reg);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace aicoder
