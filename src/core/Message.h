#pragma once
#include <string>
#include <variant>
#include <vector>
#include "core/Json.h"

namespace aicoder {

enum class Role { System, User, Assistant, Tool };

struct TextBlock       { std::string text; };
struct ToolUseBlock    { std::string id; std::string name; json input; };
struct ToolResultBlock { std::string tool_use_id; std::string content; bool is_error = false; };

using ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock>;

struct Message {
  Role role;
  std::vector<ContentBlock> content;
  // reasoning_content：deepseek-reasoner 模型专用。encodeRequest 会把它透传到下一轮的
  // role=assistant 消息的 reasoning_content 字段。普通模型（无 reasoning）此字段为空。
  std::string reasoning_content;
};

inline Message userText(std::string t) {
  return Message{Role::User, {TextBlock{std::move(t)}}};
}
inline Message systemText(std::string t) {
  return Message{Role::System, {TextBlock{std::move(t)}}};
}

// 把一条消息里所有 TextBlock 拼起来
inline std::string assistantText(const Message& m) {
  std::string out;
  for (const auto& b : m.content)
    if (const auto* tb = std::get_if<TextBlock>(&b)) out += tb->text;
  return out;
}

}
