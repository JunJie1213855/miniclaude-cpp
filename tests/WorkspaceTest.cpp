#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "workspace/Workspace.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_ws_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST(Workspace, ReadFilePresentAndMissing) {
  fs::path d = scratch();
  { std::ofstream(d / "a.txt") << "hi"; }
  EXPECT_EQ(readFile(d / "a.txt").value_or("<none>"), "hi");
  EXPECT_FALSE(readFile(d / "nope.txt").has_value());
  fs::remove_all(d);
}

TEST(Workspace, ListMarkdownRecursiveSorted) {
  fs::path d = scratch();
  fs::create_directories(d / "sub");
  { std::ofstream(d / "b.md") << "B"; }
  { std::ofstream(d / "a.md") << "A"; }
  { std::ofstream(d / "sub" / "c.md") << "C"; }
  { std::ofstream(d / "skip.txt") << "X"; }
  auto md = listMarkdown(d);
  ASSERT_EQ(md.size(), 3u);
  EXPECT_EQ(md[0].filename().string(), "a.md");
  EXPECT_EQ(md[1].filename().string(), "b.md");
  EXPECT_EQ(md[2].filename().string(), "c.md");
  EXPECT_TRUE(listMarkdown(d / "missing").empty());
  fs::remove_all(d);
}

TEST(Workspace, ParseFrontmatter) {
  auto fm = parseFrontmatter("---\nname: foo\ndescription: does X\n---\nbody line\n");
  EXPECT_EQ(fm.meta["name"], "foo");
  EXPECT_EQ(fm.meta["description"], "does X");
  EXPECT_EQ(fm.body, "body line\n");
  auto plain = parseFrontmatter("no frontmatter here");
  EXPECT_TRUE(plain.meta.empty());
  EXPECT_EQ(plain.body, "no frontmatter here");
}
