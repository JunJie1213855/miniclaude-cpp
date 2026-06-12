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

  // 一击即中(非交互)模式的两个 string 参数:default 空串,
  // 然后用 present<>() 提取 → 没传就是 nullopt(不是空串),
  // 让"给了空串"和"没给"语义不同。配对检查放 main.cpp,这里只负责解析。
  parser.add_argument("-s", "--system-prompt")
        .default_value(std::string{})
        .help("one-shot mode: text appended to the base system prompt");
  parser.add_argument("-u", "--user")
        .default_value(std::string{})
        .help("one-shot mode: user message to send");

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

  // argparse 的 present<>() 在 .default_value() 已设的 arg 上会抛
  // "always presents";改用 get<>() 拿字符串值,空串即视为未传。
  // 走的是 user 期望的"没传 = nullopt,传了 = Some(text)"语义。
  if (auto s = parser.get<std::string>("--system-prompt"); !s.empty())
    out.system_prompt = std::move(s);
  if (auto u = parser.get<std::string>("--user"); !u.empty())
    out.user_prompt = std::move(u);
  return out;
}

} // namespace aicoder
