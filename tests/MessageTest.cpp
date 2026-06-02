#include <gtest/gtest.h>
#include "core/Message.h"

using namespace aicoder;

TEST(Message, UserTextHelperBuildsUserRoleWithTextBlock) {
  Message m = userText("hello");
  EXPECT_EQ(m.role, Role::User);
  ASSERT_EQ(m.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&m.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "hello");
}

TEST(Message, ToolUseBlockHoldsNameAndInput) {
  ContentBlock b = ToolUseBlock{"id1", "read_file", json{{"path", "x"}}};
  auto* tu = std::get_if<ToolUseBlock>(&b);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "x");
}

TEST(Message, ToolResultDefaultsToNotError) {
  ToolResultBlock r{"id1", "content"};
  EXPECT_FALSE(r.is_error);
}
