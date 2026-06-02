#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "rules/Rules.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_rules_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST(Rules, ConcatGlobalThenProjectFilesAndDirs) {
  fs::path root = scratch();
  fs::path g = root / "global", p = root / "proj";
  fs::create_directories(g / "rules");
  fs::create_directories(p / ".aicoder" / "rules");
  { std::ofstream(g / "CLAUDE.md") << "GLOBAL_CLAUDE"; }
  { std::ofstream(g / "rules" / "a.md") << "GLOBAL_RULE_A"; }
  { std::ofstream(p / "CLAUDE.md") << "PROJ_CLAUDE"; }
  { std::ofstream(p / ".aicoder" / "rules" / "z.md") << "PROJ_RULE_Z"; }
  std::string r = loadRules(g, p);
  auto pos = [&](const std::string& s) { return r.find(s); };
  EXPECT_NE(pos("GLOBAL_CLAUDE"), std::string::npos);
  EXPECT_LT(pos("GLOBAL_CLAUDE"), pos("GLOBAL_RULE_A"));
  EXPECT_LT(pos("GLOBAL_RULE_A"), pos("PROJ_CLAUDE"));
  EXPECT_LT(pos("PROJ_CLAUDE"), pos("PROJ_RULE_Z"));
  fs::remove_all(root);
}

TEST(Rules, MissingSourcesSkipped) {
  fs::path root = scratch();
  EXPECT_EQ(loadRules(root / "nope_g", root / "nope_p"), "");
  fs::remove_all(root);
}

TEST(Rules, BuildSystemPromptOmitsEmptySections) {
  EXPECT_EQ(buildSystemPrompt("BASE", "", ""), "BASE");
  EXPECT_NE(buildSystemPrompt("BASE", "RULES", "").find("RULES"), std::string::npos);
  std::string full = buildSystemPrompt("BASE", "RULES", "- s: d");
  EXPECT_NE(full.find("## 可用技能"), std::string::npos);
  EXPECT_NE(full.find("- s: d"), std::string::npos);
}
