#include <gtest/gtest.h>
#include <string>
#include "core/Json.h"
#include "core/Version.h"

TEST(Smoke, JsonParses) {
  auto j = aicoder::json::parse(R"({"a":1})");
  EXPECT_EQ(j["a"].get<int>(), 1);
}

TEST(Smoke, VersionNonEmpty) {
  EXPECT_FALSE(std::string(aicoder::version()).empty());
}
