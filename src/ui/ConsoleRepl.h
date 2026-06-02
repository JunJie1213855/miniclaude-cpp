#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "core/AgentLoop.h"
#include "core/Message.h"
#include "commands/CommandRouter.h"

namespace aicoder
{

  class ConsoleRepl
  {
  public:
    ConsoleRepl(AgentLoop &loop, const CommandRouter &router, std::string systemPrompt,
                std::istream &in = std::cin, std::ostream &out = std::cout);
    void run();

  private:
    AgentLoop &loop_;
    const CommandRouter &router_;
    std::string systemPrompt_;
    std::istream &in_;
    std::ostream &out_;
    std::vector<Message> messages_;
  };
}
