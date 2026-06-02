#include <gtest/gtest.h>
#include <cstdlib>
#include "config/Config.h"
#include "core/Errors.h"

using namespace aicoder;

TEST(Config, MissingApiKeyThrows) {
  ::unsetenv("AICODER_API_KEY");
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}

TEST(Config, DefaultsWhenOnlyKeySet) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::unsetenv("AICODER_BASE_URL");
  ::unsetenv("AICODER_MODEL");
  ::unsetenv("AICODER_MAX_ITERATIONS");
  Config c = Config::fromEnv();
  EXPECT_EQ(c.api_key, "secret");
  EXPECT_EQ(c.base_url, "https://api.deepseek.com/v1");
  EXPECT_EQ(c.model, "deepseek-v4-pro");
  EXPECT_EQ(c.max_iterations, 16);
}

TEST(Config, OverridesFromEnv) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_BASE_URL", "https://x/v1", 1);
  ::setenv("AICODER_MODEL", "qwen-max", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "5", 1);
  Config c = Config::fromEnv();
  EXPECT_EQ(c.base_url, "https://x/v1");
  EXPECT_EQ(c.model, "qwen-max");
  EXPECT_EQ(c.max_iterations, 5);
}

TEST(Config, NonNumericMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "abc", 1);
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}

TEST(Config, NonPositiveMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "0", 1);
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}
