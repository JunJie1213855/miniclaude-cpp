#pragma once
#include <optional>
#include <string>

namespace aicoder {
enum class CliMode { NewSession, ContinueLatest, ResumePicker, ListSessions };

struct CliArgs {
  CliMode mode = CliMode::NewSession;
  // 一击即中(非交互)模式:两者都设才生效。沿用原 CliMode 默认 NewSession,
  // 让"完全没传新 flag"的行为和以前一模一样。
  std::optional<std::string> system_prompt;
  std::optional<std::string> user_prompt;
};

// Parses argv (argv[0] is program name). Returns nullopt on unknown flag.
// 互斥优先级（同时给多个时）：ListSessions > ResumePicker > ContinueLatest > NewSession。
// 一次性模式独立判断:system_prompt 和 user_prompt 必须配对。
std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv);

inline constexpr const char* kUsage =
    "Usage: aicoder_tui [-c | --resume | --list-sessions]\n"
    "                   [--system-prompt <text> --user <text>]\n"
    "  -c, --continue-latest  resume the most recently updated session\n"
    "  --resume               pick a past session from a list\n"
    "  --list-sessions        list saved sessions and exit (delete with: rm -rf ~/.aicoder/sessions/<id>)\n"
    "  -s, --system-prompt    one-shot mode: text appended to base system prompt\n"
    "  -u, --user             one-shot mode: user message to send\n"
    "                         (one-shot requires both --system-prompt and --user)\n";
}
