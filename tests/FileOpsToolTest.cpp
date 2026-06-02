#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/CreateFileTool.h"
#include "tools/CreateDirTool.h"
#include "tools/DeleteFileTool.h"
#include "tools/MoveFileTool.h"
#include "core/Errors.h"

using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratchDir() {
  fs::path d = fs::temp_directory_path() / "aicoder_fileops_test";
  fs::create_directories(d);
  return d;
}
}  // namespace

TEST(CreateFileTool, CreatesNewFileWithContent) {
  fs::path dir = scratchDir();
  fs::path file = dir / "new.txt";
  fs::remove(file);
  Tool t = makeCreateFileTool();
  std::string out = t.execute(json{{"path", file.string()}, {"content", "hi"}});
  EXPECT_NE(out.find("created"), std::string::npos);
  std::ifstream in(file);
  std::string body((std::istreambuf_iterator<char>(in)), {});
  EXPECT_EQ(body, "hi");
  fs::remove_all(dir);
}

TEST(CreateFileTool, FailsIfFileExists) {
  fs::path dir = scratchDir();
  fs::path file = dir / "exists.txt";
  { std::ofstream(file) << "x"; }
  Tool t = makeCreateFileTool();
  EXPECT_THROW(t.execute(json{{"path", file.string()}}), ToolError);
  fs::remove_all(dir);
}

TEST(CreateDirTool, CreatesNestedDirectories) {
  fs::path dir = scratchDir();
  fs::path nested = dir / "a" / "b" / "c";
  fs::remove_all(dir / "a");
  Tool t = makeCreateDirTool();
  t.execute(json{{"path", nested.string()}});
  EXPECT_TRUE(fs::is_directory(nested));
  fs::remove_all(dir);
}

TEST(DeleteFileTool, DeletesFileButRefusesDirectory) {
  fs::path dir = scratchDir();
  fs::path file = dir / "del.txt";
  { std::ofstream(file) << "x"; }
  Tool t = makeDeleteFileTool();
  t.execute(json{{"path", file.string()}});
  EXPECT_FALSE(fs::exists(file));
  // 拒绝删除目录
  EXPECT_THROW(t.execute(json{{"path", dir.string()}}), ToolError);
  fs::remove_all(dir);
}

TEST(MoveFileTool, RenamesFile) {
  fs::path dir = scratchDir();
  fs::path src = dir / "src.txt";
  fs::path dst = dir / "dst.txt";
  { std::ofstream(src) << "x"; }
  fs::remove(dst);
  Tool t = makeMoveFileTool();
  t.execute(json{{"source", src.string()}, {"dest", dst.string()}});
  EXPECT_FALSE(fs::exists(src));
  EXPECT_TRUE(fs::exists(dst));
  fs::remove_all(dir);
}

TEST(MoveFileTool, MissingSourceThrows) {
  Tool t = makeMoveFileTool();
  EXPECT_THROW(
      t.execute(json{{"source", "/no/such/xyz"}, {"dest", "/tmp/whatever"}}),
      ToolError);
}
