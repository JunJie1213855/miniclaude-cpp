#include "sessions/CliArgs.h"
#include <string>

namespace aicoder {
std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv) {
  CliArgs out;
  bool sawC = false, sawResume = false, sawList = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-c") sawC = true;
    else if (a == "--resume") sawResume = true;
    else if (a == "--list-sessions") sawList = true;
    else return std::nullopt;
  }
  if (sawList)        out.mode = CliMode::ListSessions;
  else if (sawResume) out.mode = CliMode::ResumePicker;
  else if (sawC)      out.mode = CliMode::ContinueLatest;
  else                out.mode = CliMode::NewSession;
  return out;
}
}
