#include "commands/CommandRouter.h"
#include "skills/SkillRegistry.h"
namespace aicoder
{
  CommandRouter::CommandRouter(CommandRegistry registry, SkillRegistry *skills,
                               std::filesystem::path skillGlobalDir,
                               std::filesystem::path skillProjectDir)
      : registry_(std::move(registry)),
        skills_(skills),
        skillGlobalDir_(std::move(skillGlobalDir)),
        skillProjectDir_(std::move(skillProjectDir)) {}
  namespace
  {
    std::string trim(const std::string &s)
    {
      size_t b = s.find_first_not_of(" \t\r\n");
      if (b == std::string::npos)
        return "";
      size_t e = s.find_last_not_of(" \t\r\n");
      return s.substr(b, e - b + 1);
    }
    std::string replaceAll(std::string s, const std::string &from, const std::string &to)
    {
      if (from.empty())
        return s;
      for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
      return s;
    }
  }
  CommandOutcome CommandRouter::handle(const std::string &input,
                                       std::vector<Message> &messages) const
  {
    std::string cmd = trim(input);
    if (cmd == "/quit" || cmd == "/exit")
      return {CommandResult::Quit, ""};
    if (cmd == "/clear")
    {
      std::vector<Message> kept;
      for (const auto &m : messages)
        if (m.role == Role::System)
          kept.push_back(m);
      messages = std::move(kept);
      return {CommandResult::Cleared, ""};
    }
    if (cmd == "/sessions")
    {
      if (sessionsCallback_)
        sessionsCallback_();
      return {CommandResult::NotACommand, ""};
    }
    if (cmd == "/reload-skills")
    {
      std::string msg;
      if (skills_)
      {
        skills_->discover(skillGlobalDir_, skillProjectDir_);
        msg = "[已重新加载技能，共 " + std::to_string(skills_->list().size()) + " 个]";
      }
      else
      {
        msg = "[未配置技能注册表，无法重新加载]";
      }
      return {CommandResult::Reloaded, msg};
    }
    if (!cmd.empty() && cmd[0] == '/')
    {
      size_t sp = cmd.find(' ');
      std::string name = cmd.substr(1, (sp == std::string::npos ? cmd.size() : sp) - 1);
      std::string args = (sp == std::string::npos) ? "" : trim(cmd.substr(sp + 1));
      if (const CommandTemplate *t = registry_.find(name))
        return {CommandResult::Prompt, replaceAll(t->body, "$ARGUMENTS", args)};
      if (skills_)
      {
        if (const SkillInfo *s = skills_->find(name))
        {
          // 技能正文有 $ARGUMENTS 占位则替换；没有但用户传了 args，自动在正文前拼一段
          // 「用户请求」让模型看见意图（旧技能无需手写 $ARGUMENTS 即可工作）。
          if (s->body.find("$ARGUMENTS") != std::string::npos)
            return {CommandResult::Prompt, replaceAll(s->body, "$ARGUMENTS", args)};
          if (!args.empty())
            return {CommandResult::Prompt, "用户请求: " + args + "\n\n" + s->body};
          return {CommandResult::Prompt, s->body};
        }
      }
    }
    return {CommandResult::NotACommand, ""};
  }
  std::vector<CommandInfo> CommandRouter::commands() const
  {
    std::vector<CommandInfo> out = {
        {"/quit", "退出"},
        {"/clear", "清空对话"},
        {"/sessions", "查看并切换历史会话"},
        {"/reload-skills", "重新加载技能"}};
    for (const auto &t : registry_.list())
      out.push_back({"/" + t.name, t.description});
    if (skills_)
    {
      for (const auto &s : skills_->list())
        out.push_back({"/" + s.name, s.description});
    }
    return out;
  }
}
