#include <gtest/gtest.h>
#include "llm/OpenAIProvider.h"

using namespace aicoder;

TEST(OpenAIProvider, EncodesSystemUserAndTools) {
  OpenAIProvider p;
  std::vector<Message> msgs = {systemText("sys"), userText("hi")};
  std::vector<ToolSpec> tools = {{"read_file", "读文件", json{{"type", "object"}}}};
  json req = p.encodeRequest(msgs, tools, "deepseek-chat");

  EXPECT_EQ(req["model"].get<std::string>(), "deepseek-chat");
  ASSERT_EQ(req["messages"].size(), 2u);
  EXPECT_EQ(req["messages"][0]["role"].get<std::string>(), "system");
  EXPECT_EQ(req["messages"][1]["role"].get<std::string>(), "user");
  ASSERT_TRUE(req.contains("tools"));
  EXPECT_EQ(req["tools"][0]["function"]["name"].get<std::string>(), "read_file");
  EXPECT_EQ(req["tool_choice"].get<std::string>(), "auto");
}

TEST(OpenAIProvider, EncodesAssistantToolCallAndToolResult) {
  OpenAIProvider p;
  Message assistant{Role::Assistant, {ToolUseBlock{"call_1", "read_file", json{{"path", "x"}}}}};
  Message toolMsg{Role::Tool, {ToolResultBlock{"call_1", "file contents", false}}};
  json req = p.encodeRequest({assistant, toolMsg}, {}, "m");

  ASSERT_EQ(req["messages"].size(), 2u);
  auto& a = req["messages"][0];
  EXPECT_EQ(a["role"].get<std::string>(), "assistant");
  EXPECT_EQ(a["tool_calls"][0]["id"].get<std::string>(), "call_1");
  EXPECT_EQ(a["tool_calls"][0]["function"]["name"].get<std::string>(), "read_file");
  auto& tr = req["messages"][1];
  EXPECT_EQ(tr["role"].get<std::string>(), "tool");
  EXPECT_EQ(tr["tool_call_id"].get<std::string>(), "call_1");
  EXPECT_EQ(tr["content"].get<std::string>(), "file contents");
}

TEST(OpenAIProvider, DecodesPlainTextResponse) {
  OpenAIProvider p;
  json body = {
    {"choices", {{
      {"finish_reason", "stop"},
      {"message", {{"role", "assistant"}, {"content", "你好"}}}
    }}}
  };
  Response r = p.decodeResponse(body);
  EXPECT_EQ(r.finish_reason, "stop");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "你好");
}

TEST(OpenAIProvider, DecodesToolCallResponse) {
  OpenAIProvider p;
  json body = {
    {"choices", {{
      {"finish_reason", "tool_calls"},
      {"message", {
        {"role", "assistant"},
        {"content", nullptr},
        {"tool_calls", {{
          {"id", "call_9"},
          {"type", "function"},
          {"function", {{"name", "read_file"}, {"arguments", "{\"path\":\"a.txt\"}"}}}
        }}}
      }}
    }}}
  };
  Response r = p.decodeResponse(body);
  EXPECT_EQ(r.finish_reason, "tool_calls");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tu = std::get_if<ToolUseBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->id, "call_9");
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "a.txt");
}

TEST(OpenAIProvider, EmptyAssistantGetsEmptyStringContentNotNull) {
  // reasoner 可能返回只有 reasoning_content、无正文无 tool_calls 的空轮。
  // 回传时 content 必须是 ""（被设置），不能是 null，否则 DeepSeek 400。
  OpenAIProvider p;
  Message assistant;
  assistant.role = Role::Assistant;
  assistant.reasoning_content = "只有思考，没有正文";
  json req = p.encodeRequest({assistant}, {}, "deepseek-reasoner");

  auto& a = req["messages"][0];
  ASSERT_TRUE(a.contains("content"));
  EXPECT_FALSE(a["content"].is_null());
  EXPECT_EQ(a["content"].get<std::string>(), "");
  EXPECT_FALSE(a.contains("tool_calls"));
}

TEST(OpenAIProvider, DecodesReasoningContent) {
  OpenAIProvider p;
  json body = {
    {"choices", {{
      {"finish_reason", "stop"},
      {"message", {
        {"role", "assistant"},
        {"reasoning_content", "分析：首先需要读取文件内容..."},
        {"content", "文件内容如下..."}
      }}
    }}}
  };
  Response r = p.decodeResponse(body);
  EXPECT_EQ(r.assistant_message.reasoning_content,
            "分析：首先需要读取文件内容...");
  // content also present
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  EXPECT_TRUE(std::get_if<TextBlock>(&r.assistant_message.content[0]) != nullptr);
}

TEST(OpenAIProvider, EncodesAssistantWithReasoningContent) {
  OpenAIProvider p;
  Message assistant;
  assistant.role = Role::Assistant;
  assistant.content.push_back(TextBlock{"最终回答"});
  assistant.reasoning_content = "思考过程：第一轮分析...";
  json req = p.encodeRequest({assistant}, {}, "deepseek-reasoner");

  ASSERT_EQ(req["messages"].size(), 1u);
  auto& a = req["messages"][0];
  EXPECT_EQ(a["role"].get<std::string>(), "assistant");
  EXPECT_EQ(a["content"].get<std::string>(), "最终回答");
  EXPECT_EQ(a["reasoning_content"].get<std::string>(), "思考过程：第一轮分析...");
}

TEST(OpenAIProvider, EncodesToolCallWithReasoningContent) {
  OpenAIProvider p;
  Message assistant;
  assistant.role = Role::Assistant;
  assistant.content.push_back(ToolUseBlock{"c1", "read_file", json{{"path", "x"}}});
  assistant.reasoning_content = "先读文件看看";
  json req = p.encodeRequest({assistant}, {}, "deepseek-reasoner");

  auto& a = req["messages"][0];
  ASSERT_TRUE(a.contains("tool_calls"));
  EXPECT_EQ(a["tool_calls"][0]["id"].get<std::string>(), "c1");
  EXPECT_EQ(a["reasoning_content"].get<std::string>(), "先读文件看看");
  // content should be null when only tool_calls
  EXPECT_TRUE(a["content"].is_null());
}

// ---- max_tokens 透传 ----

TEST(OpenAIProvider, MaxTokensZeroOmitsField) {
  // 默认(0)/不传 → 请求体里不包含 max_tokens,由服务端用模型默认上限
  OpenAIProvider p;
  std::vector<Message> msgs = {userText("hi")};
  json req = p.encodeRequest(msgs, {}, "m", /*maxTokens=*/0);
  EXPECT_FALSE(req.contains("max_tokens"));
}

TEST(OpenAIProvider, MaxTokensPositiveWritesField) {
  // max_tokens>0 → 写入请求体
  OpenAIProvider p;
  std::vector<Message> msgs = {userText("hi")};
  json req = p.encodeRequest(msgs, {}, "m", /*maxTokens=*/2048);
  ASSERT_TRUE(req.contains("max_tokens"));
  EXPECT_EQ(req["max_tokens"].get<int>(), 2048);
}
