#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/ListDirTool.h"
#include "core/Errors.h"

using namespace aicoder;
namespace fs = std::filesystem;

TEST(ListDirTool, ListsEntries) {
  fs::path dir = fs::temp_directory_path() / "aicoder_listdir_test";
  fs::create_directories(dir / "sub");
  { std::ofstream(dir / "a.txt") << "x"; }
  Tool t = makeListDirTool();
  std::string out = t.execute(json{{"path", dir.string()}});
  EXPECT_NE(out.find("a.txt"), std::string::npos);
  EXPECT_NE(out.find("sub"), std::string::npos);
  fs::remove_all(dir);
}

TEST(ListDirTool, NonDirThrowsToolError) {
  Tool t = makeListDirTool();
  EXPECT_THROW(t.execute(json{{"path", "/no/such/dir/xyz"}}), ToolError);
}

TEST(ListDirTool, HasNameAndSchema) {
  Tool t = makeListDirTool();
  EXPECT_EQ(t.name, "ls");
  EXPECT_EQ(t.input_schema["type"].get<std::string>(), "object");
}
