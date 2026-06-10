#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include "config/Config.h"
#include "core/Errors.h"
#include "workspace/Workspace.h"

using namespace aicoder;

// 在 globalDir() 写临时 settings.json，析构时还原。
class SettingsFileGuard {
public:
  explicit SettingsFileGuard(const std::string& content) {
    dir_ = globalDir();
    path_ = dir_ / "settings.json";
    // 备份旧文件
    if (std::filesystem::is_regular_file(path_)) {
      auto data = readFile(path_);
      if (data) backup_ = *data;
    }
    // 确保目录存在
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    out << content;
  }
  ~SettingsFileGuard() {
    if (backup_) {
      std::ofstream out(path_, std::ios::binary | std::ios::trunc);
      out << *backup_;
    } else {
      std::filesystem::remove(path_);
    }
  }
private:
  std::filesystem::path dir_;
  std::filesystem::path path_;
  std::optional<std::string> backup_;
};

using namespace aicoder;

TEST(Config, MissingApiKeyThrows) {
  ::unsetenv("AICODER_API_KEY");
  EXPECT_THROW(Config::fromEnv(false), ConfigError);
}

TEST(Config, DefaultsWhenOnlyKeySet) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::unsetenv("AICODER_BASE_URL");
  ::unsetenv("AICODER_MODEL");
  ::unsetenv("AICODER_MAX_ITERATIONS");
  ::unsetenv("AICODER_MAX_TOKENS");
  Config c = Config::fromEnv(false);
  EXPECT_EQ(c.api_key, "secret");
  EXPECT_EQ(c.base_url, "https://api.deepseek.com/v1");
  EXPECT_EQ(c.model, "deepseek-v4-pro");
  EXPECT_EQ(c.max_iterations, 16);
  EXPECT_EQ(c.max_tokens, 0);  // 未设置 = 不下发
}

TEST(Config, OverridesFromEnv) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_BASE_URL", "https://x/v1", 1);
  ::setenv("AICODER_MODEL", "qwen-max", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "5", 1);
  ::setenv("AICODER_MAX_TOKENS", "2048", 1);
  Config c = Config::fromEnv(false);
  EXPECT_EQ(c.base_url, "https://x/v1");
  EXPECT_EQ(c.model, "qwen-max");
  EXPECT_EQ(c.max_iterations, 5);
  EXPECT_EQ(c.max_tokens, 2048);
}

TEST(Config, NonNumericMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "abc", 1);
  EXPECT_THROW(Config::fromEnv(false), ConfigError);
}

TEST(Config, NonPositiveMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "0", 1);
  EXPECT_THROW(Config::fromEnv(false), ConfigError);
}

// ---- settings.json 导入 ----

TEST(Config, SettingsFileProvidesApiKey) {
  SettingsFileGuard guard(R"({"env":{"AICODER_AUTH_TOKEN":"sk-test"}})");
  ::unsetenv("AICODER_API_KEY");
  Config c = Config::fromEnv(true);
  EXPECT_EQ(c.api_key, "sk-test");
  EXPECT_EQ(c.base_url, "https://api.deepseek.com/v1");   // 未配置 → 默认
  EXPECT_EQ(c.model, "deepseek-v4-pro");
}

TEST(Config, EnvOverridesSettingsFile) {
  SettingsFileGuard guard(
      R"({"env":{"AICODER_AUTH_TOKEN":"sk-file","AICODER_BASE_URL":"https://file/v1","AICODER_DEFAULT_MODEL":"file-model"}})");
  ::setenv("AICODER_API_KEY", "sk-env", 1);
  ::setenv("AICODER_BASE_URL", "https://env/v1", 1);
  ::setenv("AICODER_MODEL", "env-model", 1);
  ::unsetenv("AICODER_MAX_ITERATIONS");
  Config c = Config::fromEnv(true);
  EXPECT_EQ(c.api_key, "sk-env");              // env AICODER_API_KEY 优先于 settings AICODER_AUTH_TOKEN
  EXPECT_EQ(c.base_url, "https://env/v1");    // env 覆盖 settings
  EXPECT_EQ(c.model, "env-model");            // env 覆盖 settings
}

TEST(Config, SettingsFileNullEnvDoesNotProvideFallback) {
  SettingsFileGuard guard(R"({"env":{}})");  // env 是空对象，无任何 key
  ::unsetenv("AICODER_API_KEY");
  ::unsetenv("AICODER_BASE_URL");
  ::unsetenv("AICODER_MODEL");
  ::unsetenv("AICODER_MAX_ITERATIONS");
  EXPECT_THROW(Config::fromEnv(true), ConfigError);
}

// ---- max_tokens 解析边界 ----

TEST(Config, MaxTokensZeroIsValid) {
  // 0 合法 = 不下发(由服务端用默认)
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_TOKENS", "0", 1);
  Config c = Config::fromEnv(false);
  EXPECT_EQ(c.max_tokens, 0);
}

TEST(Config, MaxTokensNegativeThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_TOKENS", "-1", 1);
  EXPECT_THROW(Config::fromEnv(false), ConfigError);
}

TEST(Config, MaxTokensNonNumericThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_TOKENS", "abc", 1);
  EXPECT_THROW(Config::fromEnv(false), ConfigError);
}
