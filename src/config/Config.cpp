#include "config/Config.h"
#include "core/Errors.h"
#include "workspace/Workspace.h"
#include <cstdlib>
#include <string>

namespace aicoder
{

  namespace
  {

    // 读取 ~/.aicoder/settings.json 的 "env" 子对象（若存在），否则 nullopt。
    std::optional<json> loadSettingsEnv()
    {
      auto path = globalDir() / "settings.json";
      auto content = readFile(path);
      if (!content)
        return std::nullopt;
      try
      {
        json j = json::parse(*content);
        if (j.contains("env") && j["env"].is_object())
          return j["env"];
      }
      catch (const json::exception &)
      { /* 格式错误就当没有 */
      }
      return std::nullopt;
    }

    // env 变量优先；未设置则回退到 settings["key"]；都没有返回 fallback。
    std::string resolve(const char *envName,
                        const std::optional<json> &settings,
                        const char *settingsKey,
                        const std::string &fallback)
    {
      const char *v = std::getenv(envName);
      if (v && *v)
        return std::string(v);
      if (settings && settings->contains(settingsKey))
      {
        const auto &val = (*settings)[settingsKey];
        if (val.is_string())
          return val.get<std::string>();
      }
      return fallback;
    }

  } // namespace

  Config Config::fromEnv(bool useSettingsFile)
  {
    auto settings = useSettingsFile ? loadSettingsEnv() : std::nullopt;

    Config c;

    // api_key：env AICODER_API_KEY 优先，其次 settings 里的 AICODER_AUTH_TOKEN
    {
      const char *v = std::getenv("AICODER_API_KEY");
      if (v && *v)
      {
        c.api_key = v;
      }
      else if (settings && settings->contains("AICODER_AUTH_TOKEN"))
      {
        const auto &val = (*settings)["AICODER_AUTH_TOKEN"];
        if (val.is_string())
          c.api_key = val.get<std::string>();
      }
    }
    if (c.api_key.empty())
      throw ConfigError(
          "请设置环境变量 AICODER_API_KEY，或在 ~/.aicoder/settings.json 中配置 "
          "AICODER_AUTH_TOKEN");

    c.base_url = resolve("AICODER_BASE_URL", settings, "AICODER_BASE_URL", c.base_url);
    c.model = resolve("AICODER_MODEL", settings, "AICODER_DEFAULT_MODEL", c.model);

    // max_iterations 仅从 env 读取（settings.json 不提供）
    {
      const char *v = std::getenv("AICODER_MAX_ITERATIONS");
      std::string mi = (v && *v) ? std::string(v) : std::to_string(c.max_iterations);
      try
      {
        size_t pos = 0;
        c.max_iterations = std::stoi(mi, &pos);
        if (pos != mi.size())
          throw std::invalid_argument(mi);
      }
      catch (const std::exception &)
      {
        throw ConfigError("AICODER_MAX_ITERATIONS 必须是整数，当前为: " + mi);
      }
      if (c.max_iterations <= 0)
        throw ConfigError("AICODER_MAX_ITERATIONS 必须是正整数，当前为: " + mi);
    }

    // max_tokens 仅从 env 读取（与 max_iterations 保持一致）。
    // 0 / 未设置 = 不下发（由服务端用模型默认）；负数 / 非数字 = 报错。
    {
      const char *v = std::getenv("AICODER_MAX_TOKENS");
      if (v && *v)
      {
        std::string raw(v);
        try
        {
          size_t pos = 0;
          c.max_tokens = std::stoi(raw, &pos);
          if (pos != raw.size())
            throw std::invalid_argument(raw);
        }
        catch (const std::exception &)
        {
          throw ConfigError("AICODER_MAX_TOKENS 必须是整数，当前为: " + raw);
        }
        if (c.max_tokens < 0)
          throw ConfigError("AICODER_MAX_TOKENS 不能为负数，当前为: " + raw);
      }
    }

    return c;
  }

}
