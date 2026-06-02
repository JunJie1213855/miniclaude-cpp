#include "sessions/Session.h"

namespace aicoder {
namespace {

constexpr int kSchemaVersion = 1;

const char* roleToStr(Role r) {
  switch (r) {
    case Role::System:    return "system";
    case Role::User:      return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool:      return "tool";
  }
  return "user";
}

std::optional<Role> roleFromStr(const std::string& s) {
  if (s == "system")    return Role::System;
  if (s == "user")      return Role::User;
  if (s == "assistant") return Role::Assistant;
  if (s == "tool")      return Role::Tool;
  return std::nullopt;
}

json blockToJson(const ContentBlock& b) {
  if (auto* t = std::get_if<TextBlock>(&b))
    return {{"type", "text"}, {"text", t->text}};
  if (auto* tu = std::get_if<ToolUseBlock>(&b))
    return {{"type", "tool_use"}, {"id", tu->id}, {"name", tu->name}, {"input", tu->input}};
  if (auto* tr = std::get_if<ToolResultBlock>(&b))
    return {{"type", "tool_result"}, {"tool_use_id", tr->tool_use_id},
            {"content", tr->content}, {"is_error", tr->is_error}};
  return json::object();
}

std::optional<ContentBlock> blockFromJson(const json& j) {
  if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return std::nullopt;
  std::string type = j["type"].get<std::string>();
  if (type == "text" && j.contains("text"))
    return ContentBlock{TextBlock{j["text"].get<std::string>()}};
  if (type == "tool_use" && j.contains("id") && j.contains("name") && j.contains("input"))
    return ContentBlock{ToolUseBlock{j["id"].get<std::string>(),
                                     j["name"].get<std::string>(),
                                     j["input"]}};
  if (type == "tool_result" && j.contains("tool_use_id") && j.contains("content"))
    return ContentBlock{ToolResultBlock{j["tool_use_id"].get<std::string>(),
                                        j["content"].get<std::string>(),
                                        j.value("is_error", false)}};
  return std::nullopt;
}

json messageToJson(const Message& m) {
  json out = {{"role", roleToStr(m.role)}, {"content", json::array()}};
  for (const auto& b : m.content) out["content"].push_back(blockToJson(b));
  if (m.role == Role::Assistant && !m.reasoning_content.empty())
    out["reasoning_content"] = m.reasoning_content;
  return out;
}

std::optional<Message> messageFromJson(const json& j) {
  if (!j.is_object() || !j.contains("role") || !j["role"].is_string()) return std::nullopt;
  auto r = roleFromStr(j["role"].get<std::string>());
  if (!r) return std::nullopt;
  Message m;
  m.role = *r;
  if (j.contains("content") && j["content"].is_array()) {
    for (const auto& bj : j["content"]) {
      if (auto b = blockFromJson(bj)) m.content.push_back(std::move(*b));
    }
  }
  if (j.contains("reasoning_content") && j["reasoning_content"].is_string())
    m.reasoning_content = j["reasoning_content"].get<std::string>();
  return m;
}

}  // namespace

json sessionToJson(const SessionData& s) {
  json out = {{"schema_version", kSchemaVersion},
              {"id", s.id},
              {"created_at", s.created_at},
              {"updated_at", s.updated_at},
              {"model", s.model},
              {"messages", json::array()}};
  for (const auto& m : s.messages) out["messages"].push_back(messageToJson(m));
  return out;
}

std::optional<SessionData> sessionFromJson(const json& j) {
  if (!j.is_object()) return std::nullopt;
  if (!j.contains("schema_version") || j["schema_version"] != kSchemaVersion) return std::nullopt;
  if (!j.contains("id") || !j.contains("created_at") || !j.contains("updated_at") ||
      !j.contains("model") || !j.contains("messages") || !j["messages"].is_array())
    return std::nullopt;
  SessionData s;
  s.id = j["id"].get<std::string>();
  s.created_at = j["created_at"].get<std::string>();
  s.updated_at = j["updated_at"].get<std::string>();
  s.model = j["model"].get<std::string>();
  for (const auto& mj : j["messages"]) {
    if (auto m = messageFromJson(mj)) s.messages.push_back(std::move(*m));
  }
  return s;
}

}  // namespace aicoder
