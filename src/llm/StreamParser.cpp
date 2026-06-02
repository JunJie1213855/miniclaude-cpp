#include "llm/StreamParser.h"

namespace aicoder {

void OpenAIStreamParser::feed(const std::string& chunk, const DeltaFn& onDelta) {
  buffer_ += chunk;
  // 按 '\n' 切出完整行，残留半行留在 buffer_
  size_t pos;
  while ((pos = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    // 去掉行尾可能的 '\r'
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) processLine(line, onDelta);
  }
}

void OpenAIStreamParser::processLine(const std::string& line, const DeltaFn& onDelta) {
  // 只处理 "data: " 开头的行；其余（event:/空注释）忽略
  const std::string prefix = "data: ";
  if (line.rfind(prefix, 0) != 0) return;
  std::string payload = line.substr(prefix.size());
  if (payload == "[DONE]") return;

  json j;
  try {
    j = json::parse(payload);
  } catch (const json::exception&) {
    return;  // 跳过无法解析的行（容错）
  }
  if (!j.contains("choices") || j["choices"].empty()) return;
  const auto& choice = j["choices"][0];

  if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
    finish_reason_ = choice["finish_reason"].get<std::string>();

  if (!choice.contains("delta")) return;
  const auto& delta = choice["delta"];

  StreamDelta out;
  if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
    std::string piece = delta["reasoning_content"].get<std::string>();
    reasoning_ += piece;
    out.reasoning = piece;
  }
  if (delta.contains("content") && delta["content"].is_string()) {
    std::string piece = delta["content"].get<std::string>();
    content_ += piece;
    out.text = piece;
  }
  if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
    for (const auto& tc : delta["tool_calls"]) {
      size_t idx = tc.value("index", 0);
      if (idx >= tools_.size()) tools_.resize(idx + 1);
      ToolAccum& acc = tools_[idx];
      if (tc.contains("id") && tc["id"].is_string())
        acc.id = tc["id"].get<std::string>();
      if (tc.contains("function")) {
        const auto& fn = tc["function"];
        if (fn.contains("name") && fn["name"].is_string())
          acc.name = fn["name"].get<std::string>();
        if (fn.contains("arguments") && fn["arguments"].is_string())
          acc.args += fn["arguments"].get<std::string>();
      }
    }
  }

  if (!out.text.empty() || !out.reasoning.empty()) onDelta(out);
}

Response OpenAIStreamParser::finish() const {
  Response r;
  r.finish_reason = finish_reason_;
  r.reasoning_content = reasoning_;

  Message am;
  am.role = Role::Assistant;
  am.reasoning_content = reasoning_;
  if (!content_.empty()) am.content.push_back(TextBlock{content_});
  for (const auto& acc : tools_) {
    if (acc.name.empty()) continue;
    ToolUseBlock tu;
    tu.id = acc.id;
    tu.name = acc.name;
    // 流可能被中途截断，导致 args 是不完整 JSON。容错：解析失败时退回空对象，
    // 让工具自己报参数错（回灌给模型），而不是抛出未捕获的 json 异常炸掉本轮。
    if (acc.args.empty()) {
      tu.input = json::object();
    } else {
      try {
        tu.input = json::parse(acc.args);
      } catch (const json::exception&) {
        tu.input = json::object();
      }
    }
    am.content.push_back(tu);
  }
  r.assistant_message = am;
  return r;
}

}
