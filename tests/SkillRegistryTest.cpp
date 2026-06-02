#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "skills/SkillRegistry.h"
using namespace aicoder;
namespace fs = std::filesystem;
namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_skillreg_test";
  fs::remove_all(d); fs::create_directories(d); return d;
}
void writeSkill(const fs::path& base, const std::string& name, const std::string& desc,
                const std::string& body) {
  fs::create_directories(base / name);
  std::ofstream(base / name / "SKILL.md")
      << "---\nname: " << name << "\ndescription: " << desc << "\n---\n" << body;
}
}
TEST(SkillRegistry, DiscoverParseAndProjectOverride) {
  fs::path root = scratch();
  fs::path g = root / "g", p = root / "p";
  writeSkill(g, "fmt", "global fmt", "GLOBAL FMT BODY");
  writeSkill(g, "test", "run tests", "TEST BODY");
  writeSkill(p, "fmt", "project fmt", "PROJECT FMT BODY");
  SkillRegistry reg;
  reg.discover(g, p);
  ASSERT_NE(reg.find("fmt"), nullptr);
  EXPECT_EQ(reg.find("fmt")->description, "project fmt");
  EXPECT_EQ(reg.find("fmt")->body, "PROJECT FMT BODY");
  EXPECT_NE(reg.find("test"), nullptr);
  EXPECT_EQ(reg.find("missing"), nullptr);
  EXPECT_NE(reg.promptList().find("fmt: project fmt"), std::string::npos);
  fs::remove_all(root);
}
