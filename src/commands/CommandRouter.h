#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "core/Message.h"
#include "commands/CommandRegistry.h"
namespace aicoder
{
  class SkillRegistry;
  using SessionsCallback = std::function<void()>;

  enum class CommandResult
  {
    Quit,
    Cleared,
    Prompt,
    Reloaded, // /reload-skills 命中；CommandOutcome::prompt 是给用户看的状态提示
    NotACommand
  };
  struct CommandOutcome
  {
    CommandResult result;
    std::string prompt; // Prompt: 发给模型的展开文本；Reloaded: 给用户看的状态信息
  };
  struct CommandInfo
  {
    std::string name;
    std::string description;
  };
  class CommandRouter
  {
  public:
    CommandRouter() = default;
    // skills 给 / 补全 + 调用；带上 skill 目录后可 /reload-skills 重新发现。
    CommandRouter(CommandRegistry registry, SkillRegistry *skills = nullptr,
                  std::filesystem::path skillGlobalDir = {},
                  std::filesystem::path skillProjectDir = {});
    CommandOutcome handle(const std::string &input, std::vector<Message> &messages) const;
    std::vector<CommandInfo> commands() const;
    void setSessionsCallback(SessionsCallback cb) { sessionsCallback_ = std::move(cb); }

  private:
    CommandRegistry registry_;
    SkillRegistry *skills_ = nullptr; // 非 const：/reload-skills 需要调 discover
    std::filesystem::path skillGlobalDir_;
    std::filesystem::path skillProjectDir_;
    SessionsCallback sessionsCallback_;
  };
}
