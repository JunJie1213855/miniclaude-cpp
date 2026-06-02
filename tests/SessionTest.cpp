#include <gtest/gtest.h>
#include "sessions/Session.h"
using namespace aicoder;

TEST(Session, RoundTripUserAssistantToolMessages) {
  SessionData s;
  s.id = "20260530_133412_a3f2";
  s.created_at = "2026-05-30T13:34:12+08:00";
  s.updated_at = "2026-05-30T13:42:00+08:00";
  s.model = "deepseek-v4-pro";
  s.messages = {
      userText("hi"),
      Message{Role::Assistant,
              {TextBlock{"thinking..."},
               ToolUseBlock{"call_1", "read_file", json{{"path", "a.cpp"}}}},
              "reasoning here"},
      Message{Role::Tool,
              {ToolResultBlock{"call_1", "(file body)", false}}},
  };

  json j = sessionToJson(s);
  EXPECT_EQ(j["schema_version"], 1);
  EXPECT_EQ(j["id"], s.id);
  EXPECT_EQ(j["model"], s.model);
  ASSERT_EQ(j["messages"].size(), 3u);
  EXPECT_EQ(j["messages"][0]["role"], "user");
  EXPECT_EQ(j["messages"][1]["role"], "assistant");
  EXPECT_EQ(j["messages"][1]["reasoning_content"], "reasoning here");
  EXPECT_EQ(j["messages"][2]["role"], "tool");
  EXPECT_EQ(j["messages"][2]["content"][0]["tool_use_id"], "call_1");

  auto back = sessionFromJson(j);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->id, s.id);
  EXPECT_EQ(back->messages.size(), 3u);
  EXPECT_EQ(back->messages[0].role, Role::User);
  EXPECT_EQ(assistantText(back->messages[1]), "thinking...");
  ASSERT_EQ(back->messages[1].content.size(), 2u);
  EXPECT_EQ(back->messages[1].reasoning_content, "reasoning here");
}

TEST(Session, ReasoningContentOmittedWhenEmpty) {
  SessionData s;
  s.id = "x"; s.created_at = "t"; s.updated_at = "t"; s.model = "m";
  s.messages = {Message{Role::Assistant, {TextBlock{"hello"}}, ""}};
  json j = sessionToJson(s);
  EXPECT_FALSE(j["messages"][0].contains("reasoning_content"));
}

TEST(Session, FromJsonRejectsUnknownSchemaVersion) {
  json j = {{"schema_version", 999}, {"id", "x"}, {"created_at", "t"},
            {"updated_at", "t"}, {"model", "m"}, {"messages", json::array()}};
  EXPECT_FALSE(sessionFromJson(j).has_value());
}

TEST(Session, FromJsonRejectsMalformedJson) {
  EXPECT_FALSE(sessionFromJson(json{{"foo", "bar"}}).has_value());
}
