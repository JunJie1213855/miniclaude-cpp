#include "ui/ConsoleRepl.h"
#include "core/Errors.h"

namespace aicoder
{

  ConsoleRepl::ConsoleRepl(AgentLoop &loop, const CommandRouter &router,
                           std::string systemPrompt, std::istream &in, std::ostream &out)
      : loop_(loop), router_(router), systemPrompt_(std::move(systemPrompt)),
        in_(in), out_(out) {}

  void ConsoleRepl::run()
  {
    messages_ = {systemText(systemPrompt_)};
    out_ << "AICoder SP1a — 输入消息，/clear 清空对话，/quit 退出\n";
    std::string line;
    while (true)
    {
      out_ << "> " << std::flush;
      if (!std::getline(in_, line))
        break; // EOF
      if (line.empty())
        continue;

      CommandOutcome cr = router_.handle(line, messages_);
      if (cr.result == CommandResult::Quit)
        break;
      if (cr.result == CommandResult::Cleared)
      {
        out_ << "[已清空对话]\n";
        continue;
      }
      if (cr.result == CommandResult::Reloaded)
      {
        out_ << cr.prompt << "\n";
        continue;
      }
      if (cr.result == CommandResult::Sessions)
      {
        out_ << "[会话切换仅在 TUI 模式下可用]\n";
        continue;
      }

      messages_.push_back(userText(cr.result == CommandResult::Prompt ? cr.prompt : line));
      try
      {
        std::string reply = loop_.run(messages_);
        out_ << reply << "\n";
      }
      catch (const LlmError &e)
      {
        out_ << "[错误] " << e.what() << "\n";
      }
    }
  }

}
