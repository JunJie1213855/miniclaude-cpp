#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include "tools/ReadFileTool.h"
#include "core/Errors.h"

using namespace aicoder;

TEST(ReadFileTool, ReadsExistingFile) {
  std::string path = std::string(std::tmpnam(nullptr));
  { std::ofstream(path) << "hello world"; }
  Tool t = makeReadFileTool();
  std::string out = t.execute(json{{"path", path}});
  EXPECT_EQ(out, "hello world");
  std::remove(path.c_str());
}

TEST(ReadFileTool, MissingFileThrowsToolError) {
  Tool t = makeReadFileTool();
  EXPECT_THROW(t.execute(json{{"path", "/no/such/file/xyz"}}), ToolError);
}

TEST(ReadFileTool, MissingPathArgThrowsToolError) {
  Tool t = makeReadFileTool();
  EXPECT_THROW(t.execute(json::object()), ToolError);
}

TEST(ReadFileTool, HasNameAndSchema) {
  Tool t = makeReadFileTool();
  EXPECT_EQ(t.name, "Read");
  EXPECT_EQ(t.input_schema["type"].get<std::string>(), "object");
}
