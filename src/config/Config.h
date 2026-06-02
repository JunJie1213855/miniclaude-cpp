#pragma once
#include <string>
#include "core/Json.h"

namespace aicoder {
struct Config {
  std::string api_key;
  std::string base_url = "https://api.deepseek.com/v1";
  std::string model = "deepseek-v4-pro";
  int max_iterations = 16;

  // 从环境变量读取配置，~/.aicoder/settings.json 作为 fallback。
  // env 优先级 > settings.json；缺 AICODER_API_KEY（且 settings 里
  // 也没有 AICODER_AUTH_TOKEN）抛 ConfigError。
  // useSettingsFile=false 可跳过 settings.json（单元测试用）。
  static Config fromEnv(bool useSettingsFile = true);
};
}
