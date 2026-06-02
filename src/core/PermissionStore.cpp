#include "core/PermissionStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef __unix__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aicoder
{

  namespace fs = std::filesystem;

  std::string PermissionStore::defaultPath()
  {
    return resolvePath({});
  }

  std::string PermissionStore::resolvePath(const std::string &override)
  {
    if (!override.empty())
      return override;
    if (const char *env = std::getenv("AICODER_PERMISSIONS_PATH"))
      return env;
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".");
    fs::path dir = base / "aicoder";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "permissions.json").string();
  }

  PermissionStore::PermissionStore() : path_(resolvePath({}))
  {
    // 允许冷启动:文件不存在 = 空规则,直接用。
    std::error_code ec;
    if (!fs::exists(path_, ec))
      return;
    try
    {
      load();
    }
    catch (...)
    {
      // 配置损坏不致命,清空继续。
      std::lock_guard<std::mutex> lock(mu_);
      rules_.clear();
    }
  }

  std::string PermissionStore::serializeInput(const json &input)
  {
    // 单行紧凑 dump,正则匹配更稳定。
    return input.dump();
  }

  void PermissionStore::load(const std::string &path)
  {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string p = path.empty() ? path_ : path;
    std::ifstream in(p);
    if (!in.is_open())
      return;
    std::stringstream buf;
    buf << in.rdbuf();
    json j;
    try
    {
      j = json::parse(buf.str());
    }
    catch (const json::parse_error &)
    {
      throw std::runtime_error("PermissionStore: invalid JSON in " + p);
    }
    rules_.clear();
    if (j.contains("rules") && j["rules"].is_array())
    {
      for (const auto &r : j["rules"])
      {
        PermissionRule rule;
        rule.tool = r.value("tool", "");
        rule.pattern = r.value("pattern", "");
        rule.reason = r.value("reason", "");
        if (!rule.tool.empty() && !rule.pattern.empty())
          rules_.push_back(std::move(rule));
      }
    }
    if (!path.empty())
      path_ = path;
  }

  void PermissionStore::save(const std::string &path) const
  {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string p = path.empty() ? path_ : path;
    json j;
    j["version"] = 1;
    j["rules"] = json::array();
    for (const auto &r : rules_)
    {
      j["rules"].push_back({{"tool", r.tool}, {"pattern", r.pattern}, {"reason", r.reason}});
    }
    fs::path target(p);
    fs::path tmp = target;
    tmp += ".tmp";
    {
      std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
      if (!out.is_open())
        throw std::runtime_error("PermissionStore: cannot write " + tmp.string());
      // 单行紧凑 dump,与 serializeInput 保持一致(否则 reload 后
      // pattern 与 input 序列化结果对不上,正则不命中)。
      out << j.dump() << '\n';
      out.flush();
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec)
      throw std::runtime_error("PermissionStore: rename failed: " + ec.message());
    // fsync 目录(尽力):POSIX-only fallback to noop.
#ifdef __unix__
    if (int fd = ::open(target.parent_path().c_str(), 0 /*O_RDONLY*/); fd >= 0)
    {
      ::fsync(fd);
      ::close(fd);
    }
#endif
  }

  bool PermissionStore::isAllowed(const std::string &tool, const json &input) const
  {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string payload = serializeInput(input);
    for (const auto &r : rules_)
    {
      if (r.tool != tool)
        continue;
      // 优先按正则匹配(pattern 由用户配置,可写 "cmake\\s.*").
      // 若正则编译失败(因为保存的是 input.dump() 字符串,内含 {}[]": 等元字符),
      // fallback 到子串字面匹配 —— 这样"存啥查啥"也能命中。
      try
      {
        std::regex re(r.pattern);
        if (std::regex_search(payload, re))
          return true;
        // 编译成功但本次不命中:继续看下一条规则(允许多规则并存)。
        // 但如果规则不是合法正则(编译失败),改用子串匹配兜底。
      }
      catch (const std::regex_error &)
      {
        if (payload.find(r.pattern) != std::string::npos)
          return true;
      }
    }
    return false;
  }

  void PermissionStore::allowForever(const std::string &tool, const json &input, const std::string &reason)
  {
    {
      std::lock_guard<std::mutex> lock(mu_);
      // 去重:完全相同 (tool, pattern) 不重复加。
      const std::string pattern = serializeInput(input);
      auto it = std::find_if(rules_.begin(), rules_.end(), [&](const PermissionRule &r)
                             { return r.tool == tool && r.pattern == pattern; });
      if (it != rules_.end())
        return;
      PermissionRule r{tool, pattern, reason};
      rules_.push_back(std::move(r));
    }
    // 不在此 save():调用方(持久化层 caller)控制 IO 时机,
    // 避免每次 AllowForever 都触发 fsync,以及意外覆盖用户已有配置。
  }

  void PermissionStore::clearForTests()
  {
    std::lock_guard<std::mutex> lock(mu_);
    rules_.clear();
  }

  std::vector<PermissionRule> PermissionStore::snapshot() const
  {
    std::lock_guard<std::mutex> lock(mu_);
    return rules_;
  }

} // namespace aicoder
