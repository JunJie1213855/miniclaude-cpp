#include "core/PermissionStore.h"
#include "core/Json.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

using namespace aicoder;
namespace fs = std::filesystem;

namespace
{
  // 让 PermissionStore 用临时文件,免得污染真实 ~/.config。
  std::string tmpPath()
  {
    auto p = fs::temp_directory_path() /
             ("aicoder_perm_" + std::to_string(::getpid()) + "_" +
              std::to_string(reinterpret_cast<uintptr_t>(&tmpPath)) + ".json");
    std::error_code ec;
    fs::remove(p, ec);
    return p.string();
  }
}

TEST(PermissionStore, SaveAndReloadRoundtrip) {
  const std::string p = tmpPath();
  {
    PermissionStore s;
    s.clearForTests();
    s.allowForever("Bash", json{{"command", "cmake -S . -B build"}}, "build setup");
    s.save(p);
  }
  ASSERT_TRUE(fs::exists(p));
  PermissionStore s2;
  s2.load(p);
  EXPECT_TRUE(s2.isAllowed("Bash", json{{"command", "cmake -S . -B build"}}));
  EXPECT_FALSE(s2.isAllowed("Bash", json{{"command", "rm -rf /"}}));
  EXPECT_FALSE(s2.isAllowed("WriteFile", json{{"command", "cmake -S . -B build"}}));
  fs::remove(p);
}

TEST(PermissionStore, NoMatchReturnsFalse) {
  PermissionStore s;
  s.clearForTests();
  EXPECT_FALSE(s.isAllowed("Bash", json::object()));
}

TEST(PermissionStore, AllowForeverIsIdempotent) {
  const std::string p = tmpPath();
  PermissionStore s;
  s.clearForTests();
  s.allowForever("Bash", json{{"command", "x"}});
  s.allowForever("Bash", json{{"command", "x"}});  // 重复
  s.save(p);
  PermissionStore s2;
  s2.load(p);
  EXPECT_EQ(s2.snapshot().size(), 1u);
  fs::remove(p);
}
