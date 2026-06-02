#include <gtest/gtest.h>
#include <sstream>
#include <functional>
#include "ui/ConsoleRepl.h"
#include "core/AgentLoop.h"
#include "core/ToolRegistry.h"
#include "commands/CommandRouter.h"
#include "llm/LlmClient.h"

using namespace aicoder;

namespace {
class FakeLlmClient : public LlmClient {
public:
  explicit FakeLlmClient(std::string reply) : reply_(std::move(reply)) {}
  Response sendStream(const std::vector<Message>&, const std::vector<ToolSpec>&,
                      const DeltaCallback&) override {
    return Response{Message{Role::Assistant, {TextBlock{reply_}}}, "", "stop"};
  }
private:
  std::string reply_;
};
}

TEST(ConsoleRepl, PrintsAssistantReplyThenQuits) {
  FakeLlmClient fake("机器人回答");
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  CommandRouter router;
  std::istringstream in("你好\n/quit\n");
  std::ostringstream out;
  ConsoleRepl repl(loop, router, "sys", in, out);
  repl.run();
  EXPECT_NE(out.str().find("机器人回答"), std::string::npos);
}

TEST(ConsoleRepl, ClearCommandPrintsNotice) {
  FakeLlmClient fake("x");
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  CommandRouter router;
  std::istringstream in("/clear\n/quit\n");
  std::ostringstream out;
  ConsoleRepl repl(loop, router, "sys", in, out);
  repl.run();
  EXPECT_NE(out.str().find("已清空"), std::string::npos);
}
