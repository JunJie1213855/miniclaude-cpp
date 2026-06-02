#pragma once
#include <string>

namespace aicoder {
struct Config {
  std::string api_key;
  std::string base_url = "https://api.deepseek.com/v1";
  std::string model = "deepseek-v4-pro";
  int max_iterations = 16;

  static Config fromEnv();   // 缺 AICODER_API_KEY 抛 ConfigError
};
}
