#include "commands/CreateCommands.h"
#include "workspace/Workspace.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace aicoder
{
  namespace fs = std::filesystem;

  namespace
  {
    std::string trimWs(const std::string &s)
    {
      size_t b = s.find_first_not_of(" \t\r\n");
      if (b == std::string::npos)
        return "";
      size_t e = s.find_last_not_of(" \t\r\n");
      return s.substr(b, e - b + 1);
    }

    CreateOutcome makeErrorOutcome(const std::string &err)
    {
      CreateOutcome o;
      o.error = err;
      return o;
    }

    bool writeResource(const fs::path &path,
                       const std::string &name,
                       const std::string &description,
                       const std::string &body,
                       const std::string &ruleMode,
                       std::string &err)
    {
      err.clear();
      std::error_code ec;
      if (fs::exists(path, ec))
      {
        err = "[文件已存在: " + path.string() + "]";
        return false;
      }
      fs::path parent = path.parent_path();
      if (!parent.empty() && !fs::exists(parent, ec))
      {
        fs::create_directories(parent, ec);
        if (ec)
        {
          err = "[无法创建目录: " + parent.string() + "]";
          return false;
        }
      }
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out)
      {
        err = "[无法写入文件: " + path.string() + "]";
        return false;
      }
      out << "---\n";
      out << "name: " << name << "\n";
      if (!(ruleMode == "rule" && description.empty()))
      {
        out << "description: " << description << "\n";
      }
      out << "---\n";
      out << body;
      if (!body.empty() && body.back() != '\n')
        out << "\n";
      out.close();
      if (out.fail())
      {
        err = "[写入失败: " + path.string() + "]";
        return false;
      }
      return true;
    }

  } // namespace

  std::optional<CreateArgs> parseCreateArgs(const std::string &arg)
  {
    std::string s = trimWs(arg);
    if (s.empty())
      return std::nullopt;

    size_t pipe = s.find('|');
    std::string head;
    std::string body;
    if (pipe == std::string::npos)
    {
      head = s;
      body = "";
    }
    else
    {
      head = trimWs(s.substr(0, pipe));
      body = trimWs(s.substr(pipe + 1));
    }

    if (head.empty())
      return std::nullopt;

    size_t sp = head.find_first_of(" \t");
    std::string name;
    std::string description;
    if (sp == std::string::npos)
    {
      name = head;
      description = "";
    }
    else
    {
      name = head.substr(0, sp);
      description = trimWs(head.substr(sp + 1));
    }

    if (name.empty())
      return std::nullopt;

    return CreateArgs{name, description, body};
  }

  bool isValidResourceName(const std::string &name)
  {
    if (name.empty() || name.size() > 64)
      return false;
    auto isAlnum = [](char c)
    {
      return std::isalnum(static_cast<unsigned char>(c)) != 0;
    };
    auto isAlnumUS = [&](char c)
    {
      return isAlnum(c) || c == '_';
    };
    if (!isAlnumUS(name.front()))
      return false;
    for (size_t i = 1; i < name.size(); ++i)
    {
      char c = name[i];
      if (!(isAlnumUS(c) || c == '-'))
        return false;
    }
    return true;
  }

  CreateOutcome createSkill(const std::string &name, const std::string &description,
                            const std::string &body, const fs::path &globalSkillsDir)
  {
    if (name.empty())
      return makeErrorOutcome("[缺少名称]");
    if (!isValidResourceName(name))
      return makeErrorOutcome("[名称非法]");
    if (trimWs(body).empty())
      return makeErrorOutcome("[正文为空]");

    fs::path path = globalSkillsDir / name / "SKILL.md";
    std::string err;
    if (!writeResource(path, name, description, body, "", err))
      return makeErrorOutcome(err);

    CreateOutcome o;
    o.path = path;
    o.name = name;
    o.description = description;
    o.body = body;
    o.success = "[已创建技能 " + name + ": " + path.string() + "]";
    return o;
  }

  CreateOutcome createCommand(const std::string &name, const std::string &description,
                              const std::string &body, const fs::path &globalCommandsDir)
  {
    if (name.empty())
      return makeErrorOutcome("[缺少名称]");
    if (!isValidResourceName(name))
      return makeErrorOutcome("[名称非法]");
    if (trimWs(body).empty())
      return makeErrorOutcome("[正文为空]");

    fs::path path = globalCommandsDir / (name + ".md");
    std::string err;
    if (!writeResource(path, name, description, body, "", err))
      return makeErrorOutcome(err);

    CreateOutcome o;
    o.path = path;
    o.name = name;
    o.description = description;
    o.body = body;
    o.success = "[已创建命令 " + name + ": " + path.string() + "]";
    return o;
  }

  CreateOutcome createAgent(const std::string &name, const std::string &description,
                            const std::string &body, const fs::path &globalAgentsDir)
  {
    if (name.empty())
      return makeErrorOutcome("[缺少名称]");
    if (!isValidResourceName(name))
      return makeErrorOutcome("[名称非法]");
    if (trimWs(body).empty())
      return makeErrorOutcome("[正文为空]");

    fs::path path = globalAgentsDir / name / "AGENT.md";
    std::string err;
    if (!writeResource(path, name, description, body, "", err))
      return makeErrorOutcome(err);

    CreateOutcome o;
    o.path = path;
    o.name = name;
    o.description = description;
    o.body = body;
    o.success = "[已创建子代理 " + name + ": " + path.string() + "]";
    return o;
  }

  CreateOutcome createRule(const std::string &name, const std::string &description,
                           const std::string &body, const fs::path &globalRulesDir)
  {
    if (name.empty())
      return makeErrorOutcome("[缺少名称]");
    if (!isValidResourceName(name))
      return makeErrorOutcome("[名称非法]");
    if (trimWs(body).empty())
      return makeErrorOutcome("[正文为空]");

    fs::path path = globalRulesDir / (name + ".md");
    std::string err;
    if (!writeResource(path, name, description, body, "rule", err))
      return makeErrorOutcome(err);

    CreateOutcome o;
    o.path = path;
    o.name = name;
    o.description = description;
    o.body = body;
    o.success = "[已创建规则 " + name + ": " + path.string() + "]";
    return o;
  }

} // namespace aicoder
