#include <gtest/gtest.h>
#include "tools/BashTool.h"
#include "core/Errors.h"

using namespace aicoder;

TEST(BashTool, RunsCommandAndReturnsOutput) {
  Tool t = makeBashTool();
  std::string out = t.execute(json{{"command", "echo hello_bash_tool"}});
  EXPECT_NE(out.find("hello_bash_tool"), std::string::npos);
}

TEST(BashTool, NonZeroExitIsReported) {
  Tool t = makeBashTool();
  std::string out = t.execute(json{{"command", "exit 3"}});
  EXPECT_NE(out.find("退出码: 3"), std::string::npos);
}

TEST(BashTool, MissingCommandThrowsToolError) {
  Tool t = makeBashTool();
  EXPECT_THROW(t.execute(json::object()), ToolError);
}

TEST(BashTool, HasNameAndNeedsPermission) {
  Tool t = makeBashTool();
  EXPECT_EQ(t.name, "Bash");
  EXPECT_TRUE(t.needsPermission);
}
