#include "llm/OpenAIProvider.h"

namespace aicoder {

static const char* roleStr(Role r) {
  switch (r) {
    case Role::System: return "system";
    case Role::User: return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool: return "tool";
  }
  return "user";
}

json OpenAIProvider::encodeRequest(const std::vector<Message>& messages,
                                   const std::vector<ToolSpec>& tools,
                                   const std::string& model) const {
  json j;
  j["model"] = model;
  j["messages"] = json::array();

  for (const auto& msg : messages) {
    if (msg.role == Role::Tool) {
      // 每个 ToolResultBlock 展开成一条 role=tool 消息
      for (const auto& b : msg.content) {
        if (const auto* tr = std::get_if<ToolResultBlock>(&b)) {
          j["messages"].push_back({
            {"role", "tool"},
            {"tool_call_id", tr->tool_use_id},
            {"content", tr->content}
          });
        }
      }
      continue;
    }
    if (msg.role == Role::Assistant) {
      std::string text;
      json toolCalls = json::array();
      for (const auto& b : msg.content) {
        if (const auto* tb = std::get_if<TextBlock>(&b)) {
          text += tb->text;
        } else if (const auto* tu = std::get_if<ToolUseBlock>(&b)) {
          toolCalls.push_back({
            {"id", tu->id},
            {"type", "function"},
            {"function", {{"name", tu->name}, {"arguments", tu->input.dump()}}}
          });
        }
      }
      json m{{"role", "assistant"}};
      if (!toolCalls.empty()) m["tool_calls"] = toolCalls;
      // DeepSeek 要求 content 或 tool_calls 至少有一个被设置（reasoning_content 不算）。
      // 有正文 → 用正文；无正文但有 tool_calls → content=null（合法）；
      // 两者都没有（例如 reasoner 只返回 reasoning_content 的空轮）→ 用空串占位，
      // 否则会收到 HTTP 400 "content or tool_calls must be set"。
      if (!text.empty()) m["content"] = text;
      else if (!toolCalls.empty()) m["content"] = nullptr;
      else m["content"] = "";
      if (!msg.reasoning_content.empty())
        m["reasoning_content"] = msg.reasoning_content;
      j["messages"].push_back(m);
      continue;
    }
    // System / User：拼接所有 TextBlock
    std::string text;
    for (const auto& b : msg.content)
      if (const auto* tb = std::get_if<TextBlock>(&b)) text += tb->text;
    j["messages"].push_back({{"role", roleStr(msg.role)}, {"content", text}});
  }

  if (!tools.empty()) {
    j["tools"] = json::array();
    for (const auto& t : tools) {
      j["tools"].push_back({
        {"type", "function"},
        {"function", {
          {"name", t.name},
          {"description", t.description},
          {"parameters", t.input_schema}
        }}
      });
    }
    j["tool_choice"] = "auto";
  }
  return j;
}

Response OpenAIProvider::decodeResponse(const json& body) const {
  const auto& choice = body.at("choices").at(0);
  Response r;
  r.finish_reason = choice.value("finish_reason", std::string());

  const auto& m = choice.at("message");
  Message am;
  am.role = Role::Assistant;

  if (m.contains("reasoning_content") && !m["reasoning_content"].is_null()) {
    am.reasoning_content = m["reasoning_content"].get<std::string>();
  }

  if (m.contains("content") && !m["content"].is_null()) {
    std::string c = m["content"].get<std::string>();
    if (!c.empty()) am.content.push_back(TextBlock{c});
  }
  if (m.contains("tool_calls") && m["tool_calls"].is_array()) {
    for (const auto& tc : m["tool_calls"]) {
      ToolUseBlock tu;
      tu.id = tc.value("id", std::string());
      tu.name = tc.at("function").at("name").get<std::string>();
      std::string args = tc.at("function").value("arguments", std::string("{}"));
      tu.input = args.empty() ? json::object() : json::parse(args);
      am.content.push_back(tu);
    }
  }
  r.assistant_message = am;
  return r;
}

}
