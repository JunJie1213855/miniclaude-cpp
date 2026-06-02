#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "llm/StreamParser.h"

using namespace aicoder;

namespace {
// 收集 onDelta 回调
struct Collector {
  std::string text;
  std::string reasoning;
  void operator()(const StreamDelta& d) { text += d.text; reasoning += d.reasoning; }
};
}

TEST(StreamParser, AccumulatesPlainTextAcrossChunks) {
  OpenAIStreamParser p;
  Collector c;
  // 两条 data 行在一个 chunk
  p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n\n"
         "data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n",
         std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n", std::ref(c));
  p.feed("data: [DONE]\n\n", std::ref(c));

  EXPECT_EQ(c.text, "你好");
  Response r = p.finish();
  EXPECT_EQ(r.finish_reason, "stop");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "你好");
}

TEST(StreamParser, HandlesLineSplitAcrossChunks) {
  OpenAIStreamParser p;
  Collector c;
  // 一行被截成两个 chunk
  p.feed("data: {\"choices\":[{\"delta\":{\"cont", std::ref(c));
  p.feed("ent\":\"X\"}}]}\n\n", std::ref(c));
  EXPECT_EQ(c.text, "X");
}

TEST(StreamParser, AccumulatesReasoningContent) {
  OpenAIStreamParser p;
  Collector c;
  p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"答\"}}]}\n\n", std::ref(c));
  EXPECT_EQ(c.reasoning, "想想");
  EXPECT_EQ(c.text, "答");
  Response r = p.finish();
  EXPECT_EQ(r.reasoning_content, "想想");
}

TEST(StreamParser, TruncatedToolArgsFallsBackToEmptyObject) {
  // 流中途截断：tool_call 的 arguments 是不完整 JSON。finish() 不应抛异常，
  // 退回空对象（让工具自己报参数缺失），而不是炸掉本轮。
  OpenAIStreamParser p;
  Collector c;
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\","
         "\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"pa\"}}]}}]}\n\n",
         std::ref(c));
  Response r;
  EXPECT_NO_THROW(r = p.finish());
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tu = std::get_if<ToolUseBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_TRUE(tu->input.is_object());
  EXPECT_TRUE(tu->input.empty());
}

TEST(StreamParser, AccumulatesStreamedToolCall) {
  OpenAIStreamParser p;
  Collector c;
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
         "\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"\"}}]}}]}\n\n",
         std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"function\":{\"arguments\":\"{\\\"pa\"}}]}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"function\":{\"arguments\":\"th\\\":\\\"x\\\"}\"}}]}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n", std::ref(c));

  Response r = p.finish();
  EXPECT_EQ(r.finish_reason, "tool_calls");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tu = std::get_if<ToolUseBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->id, "call_1");
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "x");
}
