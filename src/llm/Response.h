#pragma once
#include <string>
#include "core/Message.h"

namespace aicoder {
struct Response {
  Message assistant_message;   // role=Assistant，含 TextBlock 和/或 ToolUseBlock
  std::string reasoning_content; // deepseek-reasoner 模型的思维链内容（可能为空）
  std::string finish_reason;    // "stop" / "tool_calls" / ...
};
}
