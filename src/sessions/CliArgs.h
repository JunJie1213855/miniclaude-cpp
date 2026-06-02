#pragma once
#include <optional>
#include <string>

namespace aicoder {
enum class CliMode { NewSession, ContinueLatest, ResumePicker, ListSessions };
struct CliArgs { CliMode mode = CliMode::NewSession; };

// Parses argv (argv[0] is program name). Returns nullopt on unknown flag.
// 互斥优先级（同时给多个时）：ListSessions > ResumePicker > ContinueLatest > NewSession。
std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv);

inline constexpr const char* kUsage =
    "Usage: aicoder_tui [-c | --resume | --list-sessions]\n"
    "  -c               resume the most recently updated session\n"
    "  --resume         pick a past session from a list\n"
    "  --list-sessions  list saved sessions and exit (delete with: rm -rf ~/.aicoder/sessions/<id>)\n";
}
