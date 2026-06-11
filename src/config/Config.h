#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/Json.h"

namespace aicoder {

struct McpServerConfigRaw {
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::string description;
};

struct Config {
  std::string api_key;
  std::string base_url = "https://api.deepseek.com/v1";
  std::string model = "deepseek-v4-pro";
  int max_iterations = 16;
  // 单次 LLM 调用的最大输出 token。0 = 不下发该字段(由服务端用模型默认)。
  // 通过环境变量 AICODER_MAX_TOKENS 或 settings.json["env"]["AICODER_MAX_TOKENS"] 覆盖。
  int max_tokens = 0;
  std::map<std::string, McpServerConfigRaw> mcp_servers;

  // 从环境变量读取配置，~/.aicoder/settings.json 作为 fallback。
  // env 优先级 > settings.json；缺 AICODER_API_KEY（且 settings 里
  // 也没有 AICODER_AUTH_TOKEN）抛 ConfigError。
  // useSettingsFile=false 可跳过 settings.json（单元测试用）。
  static Config fromEnv(bool useSettingsFile = true);
};

}
