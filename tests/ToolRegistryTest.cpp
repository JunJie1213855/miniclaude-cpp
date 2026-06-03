#include <gtest/gtest.h>
#include "core/ToolRegistry.h"
#include "core/Errors.h"

using namespace aicoder;

static Tool okTool() {
  Tool t;
  t.name = "echo";
  t.description = "返回输入的 text";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json& in) { return in.value("text", std::string("")); };
  return t;
}

static Tool boomTool() {
  Tool t;
  t.name = "boom";
  t.description = "总是失败";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json&) -> std::string { throw ToolError("炸了"); };
  return t;
}

TEST(ToolRegistry, RegisterAndHas) {
  ToolRegistry r;
  r.registerTool(okTool());
  EXPECT_TRUE(r.has("echo"));
  EXPECT_FALSE(r.has("nope"));
}

TEST(ToolRegistry, SpecsListsRegisteredTools) {
  ToolRegistry r;
  r.registerTool(okTool());
  auto specs = r.specs();
  ASSERT_EQ(specs.size(), 1u);
  EXPECT_EQ(specs[0].name, "echo");
}

TEST(ToolRegistry, InvokeSuccessReturnsResult) {
  ToolRegistry r;
  r.registerTool(okTool());
  ToolResultBlock res = r.invoke("u1", "echo", json{{"text", "hi"}});
  EXPECT_EQ(res.tool_use_id, "u1");
  EXPECT_EQ(res.content, "hi");
  EXPECT_FALSE(res.is_error);
}

TEST(ToolRegistry, InvokeUnknownToolReturnsErrorResult) {
  ToolRegistry r;
  ToolResultBlock res = r.invoke("u1", "ghost", json::object());
  EXPECT_TRUE(res.is_error);
  EXPECT_NE(res.content.find("ghost"), std::string::npos);
}

TEST(ToolRegistry, InvokeToolErrorIsCaughtAndWrapped) {
  ToolRegistry r;
  r.registerTool(boomTool());
  ToolResultBlock res = r.invoke("u1", "boom", json::object());
  EXPECT_TRUE(res.is_error);
  EXPECT_NE(res.content.find("炸了"), std::string::npos);
}

TEST(ToolRegistry, NeedsPermissionQuery) {
  ToolRegistry r;
  Tool rw = okTool();
  rw.name = "danger";
  rw.needsPermission = true;
  r.registerTool(okTool());  // echo: needsPermission=false (默认)
  r.registerTool(rw);        // danger: needsPermission=true
  EXPECT_FALSE(r.needsPermission("echo"));
  EXPECT_TRUE(r.needsPermission("danger"));
  EXPECT_FALSE(r.needsPermission("nonexistent"));  // 未知工具 → false
}

TEST(ToolRegistry, FilterIncludesNamedTools) {
  ToolRegistry r;
  r.registerTool(okTool());
  Tool echo2 = okTool();
  echo2.name = "echo2";
  r.registerTool(echo2);

  ToolRegistry filtered = r.filter({"echo"});
  auto specs = filtered.specs();
  ASSERT_EQ(specs.size(), 1u);
  EXPECT_EQ(specs[0].name, "echo");
}

TEST(ToolRegistry, FilterExcludesUnnamedTools) {
  ToolRegistry r;
  r.registerTool(okTool());
  Tool echo2 = okTool();
  echo2.name = "echo2";
  r.registerTool(echo2);

  ToolRegistry filtered = r.filter({"echo"});
  EXPECT_FALSE(filtered.has("echo2"));
  EXPECT_TRUE(filtered.has("echo"));
}

TEST(ToolRegistry, FilterPreservesToolCallbacks) {
  ToolRegistry r;
  r.registerTool(okTool());

  ToolRegistry filtered = r.filter({"echo"});
  ToolResultBlock res = filtered.invoke("u1", "echo", json{{"text", "filtered"}});
  EXPECT_FALSE(res.is_error);
  EXPECT_EQ(res.content, "filtered");
}

TEST(ToolRegistry, FilterPreservesNeedsPermission) {
  ToolRegistry r;
  Tool danger = okTool();
  danger.name = "danger";
  danger.needsPermission = true;
  r.registerTool(danger);

  ToolRegistry filtered = r.filter({"danger"});
  EXPECT_TRUE(filtered.needsPermission("danger"));
}
