#include "sessions/CliArgs.h"
#include "argparse/argparse.hpp"
#include <string>

namespace aicoder {

std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv) {
  argparse::ArgumentParser parser("aicoder", "1.0");

  // 三个互相独立的 flag;优先级在末尾手动解析(沿用旧实现),
  // 不放进 mutually_exclusive_group —— 老测试要求"多 flag 同给
  // 不报错,按优先级胜出",严格互斥会破坏 ResumeWinsWhenBoth。
  parser.add_argument("-c", "--continue-latest")
        .flag()
        .help("resume the most recently updated session");
  parser.add_argument("--resume")
        .flag()
        .help("pick a past session from a list");
  parser.add_argument("--list-sessions")
        .flag()
        .help("list saved sessions and exit");

  // 解析失败(未知 flag / 缺值)→ 返回 nullopt,沿用旧契约
  // (老测试 UnknownArgReturnsNullopt 期待 nullopt;main.cpp 看到 nullopt
  //  会 print kUsage 然后 return 2)。
  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception&) {
    return std::nullopt;
  }

  CliArgs out;
  const bool sawC      = parser.is_used("-c");
  const bool sawResume = parser.is_used("--resume");
  const bool sawList   = parser.is_used("--list-sessions");
  // 优先级 ListSessions > ResumePicker > ContinueLatest > NewSession
  if      (sawList)   out.mode = CliMode::ListSessions;
  else if (sawResume) out.mode = CliMode::ResumePicker;
  else if (sawC)      out.mode = CliMode::ContinueLatest;
  else                out.mode = CliMode::NewSession;
  return out;
}

} // namespace aicoder
