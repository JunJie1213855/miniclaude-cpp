#include "config/Config.h"
#include "core/Errors.h"
#include <cstdlib>
#include <string>

namespace aicoder {

static std::string envOr(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

Config Config::fromEnv() {
  Config c;
  const char* key = std::getenv("AICODER_API_KEY");
  if (!key || !*key)
    throw ConfigError("请设置环境变量 AICODER_API_KEY");
  c.api_key = key;
  // fallback 用结构体内的默认值（单一事实来源），避免在此重复字面量
  c.base_url = envOr("AICODER_BASE_URL", c.base_url);
  c.model = envOr("AICODER_MODEL", c.model);
  std::string mi = envOr("AICODER_MAX_ITERATIONS", std::to_string(c.max_iterations));
  try {
    size_t pos = 0;
    c.max_iterations = std::stoi(mi, &pos);
    if (pos != mi.size()) throw std::invalid_argument(mi);
  } catch (const std::exception&) {
    throw ConfigError("AICODER_MAX_ITERATIONS 必须是整数，当前为: " + mi);
  }
  if (c.max_iterations <= 0)
    throw ConfigError("AICODER_MAX_ITERATIONS 必须是正整数，当前为: " + mi);
  return c;
}

}
