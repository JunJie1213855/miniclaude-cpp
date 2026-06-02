#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "skills/SkillTool.h"
#include "skills/SkillRegistry.h"
#include "core/Errors.h"
using namespace aicoder;
namespace fs = std::filesystem;
TEST(SkillTool, ReturnsBodyAndErrorsOnUnknown) {
  fs::path root = fs::temp_directory_path() / "aicoder_skilltool_test";
  fs::remove_all(root);
  fs::create_directories(root / "g" / "fmt");
  std::ofstream(root / "g" / "fmt" / "SKILL.md")
      << "---\nname: fmt\ndescription: d\n---\nFORMAT INSTRUCTIONS";
  SkillRegistry reg; reg.discover(root / "g", root / "p");
  Tool t = makeSkillTool(reg);
  EXPECT_EQ(t.name, "skill");
  EXPECT_FALSE(t.needsPermission);
  EXPECT_NE(t.execute(json{{"name", "fmt"}}).find("FORMAT INSTRUCTIONS"), std::string::npos);
  EXPECT_THROW(t.execute(json{{"name", "nope"}}), ToolError);
  fs::remove_all(root);
}
