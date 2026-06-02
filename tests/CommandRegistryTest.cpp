#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "commands/CommandRegistry.h"
using namespace aicoder;
namespace fs = std::filesystem;
namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_cmdreg_test";
  fs::remove_all(d); fs::create_directories(d); return d;
}
}
TEST(CommandRegistry, DiscoversAndProjectOverridesGlobal) {
  fs::path root = scratch();
  fs::path g = root / "g", p = root / "p";
  fs::create_directories(g); fs::create_directories(p);
  { std::ofstream(g / "review.md") << "---\ndescription: global review\n---\nGLOBAL BODY $ARGUMENTS"; }
  { std::ofstream(g / "explain.md") << "explain body"; }
  { std::ofstream(p / "review.md") << "---\ndescription: project review\n---\nPROJECT BODY $ARGUMENTS"; }
  CommandRegistry reg;
  reg.discover(g, p);
  ASSERT_NE(reg.find("review"), nullptr);
  EXPECT_EQ(reg.find("review")->description, "project review");
  EXPECT_NE(reg.find("review")->body.find("PROJECT BODY"), std::string::npos);
  EXPECT_NE(reg.find("explain"), nullptr);
  EXPECT_EQ(reg.find("missing"), nullptr);
  EXPECT_EQ(reg.list().size(), 2u);
  fs::remove_all(root);
}
