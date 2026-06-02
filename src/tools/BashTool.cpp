// popen/pclose 是 POSIX 接口；-std=c++20（非 gnu++20）下需显式开启声明。
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tools/BashTool.h"
#include "core/Errors.h"
#include <cstdio>
#include <string>
#include <sys/wait.h>

namespace aicoder
{
  namespace
  {
    constexpr int kBashTimeoutSec = 30;
    constexpr size_t kBashMaxOutput = 32 * 1024; // 输出上限 32KB，超出截断

    // 把任意命令安全地包进单引号供 bash -c 使用（内嵌单引号转义为 '\''）。
    std::string shellSingleQuote(const std::string &s)
    {
      std::string out = "'";
      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out += c;
      }
      out += "'";
      return out;
    }
  } // namespace

  Tool makeBashTool()
  {
    Tool t;
    t.name = "Bash";
    t.description =
        "Run a shell command and return its combined stdout+stderr. "
        "Use for build, tests, git, and other shell operations. "
        "Times out after 30s; very large output is truncated.";
    t.input_schema = json::parse(R"({
      "type": "object",
      "properties": {
        "command": {"type": "string", "description": "Shell command to execute"}
      },
      "required": ["command"]
  })");
    t.needsPermission = true;
    t.execute = [](const json &input) -> std::string
    {
      if (!input.contains("command") || !input["command"].is_string())
        throw ToolError("missing command");
      std::string command = input["command"].get<std::string>();
      if (command.empty())
        throw ToolError("command cannot be empty");

      // timeout 限时 + bash -c 跑完整命令（保留管道/重定向语义）+ 合并 stderr。
      std::string full = "timeout " + std::to_string(kBashTimeoutSec) +
                         " bash -c " + shellSingleQuote(command) + " 2>&1";
      FILE *pipe = popen(full.c_str(), "r");
      if (!pipe)
        throw ToolError("failed to start command");

      std::string out;
      char buf[4096];
      size_t n;
      bool truncated = false;
      while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
      {
        out.append(buf, n);
        if (out.size() >= kBashMaxOutput)
        {
          out.resize(kBashMaxOutput);
          truncated = true;
          break;
        }
      }
      int status = pclose(pipe);
      int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

      if (truncated)
        out += "\n...[输出过长，已截断]";
      if (code == 124)
        out += "\n[命令超时（>" + std::to_string(kBashTimeoutSec) + "s），已终止]";
      else if (code != 0)
        out += "\n[退出码: " + std::to_string(code) + "]";
      return out.empty() ? "(命令无输出，退出码 0)" : out;
    };
    return t;
  }
} // namespace aicoder
