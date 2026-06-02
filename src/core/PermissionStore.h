#pragma once
#include <string>
#include <vector>
#include <regex>
#include <mutex>
#include "core/Json.h"

namespace aicoder
{

  // 一条"放行"规则:工具 + 命中的参数模式。
  // pattern 是基于整个 ToolUseBlock 的 input(json)序列化后做正则匹配。
  // 对所有受 needsPermission 标记的工具适用。
  struct PermissionRule
  {
    std::string tool;    // 工具函数名,例如 "Bash" / "WriteFile"
    std::string pattern; // 正则,匹配 input.dump()
    std::string reason;  // 可读备注(用户当时选了 AllowForever 的时间或理由)
  };

  // 持久化 ~/.config/aicoder/permissions.json(可用 $XDG_CONFIG_HOME 覆盖)。
  // 线程安全;写入是 fsync 后 reload,读走 in-memory cache。
  class PermissionStore
  {
  public:
    PermissionStore();

    // 文件默认路径(可被环境变量 AICODER_PERMISSIONS_PATH 覆盖,便于测试)。
    static std::string defaultPath();

    // 从 path 读;若不存在,初始化为空。失败抛 std::runtime_error。
    void load(const std::string &path = {});
    // 写回 path(原子:写 .tmp + rename + fsync dir)。
    void save(const std::string &path = {}) const;

    // 工具的 input 是否命中任一规则(纯函数,无 IO)。
    bool isAllowed(const std::string &tool, const json &input) const;

    // 添加并落盘。
    void allowForever(const std::string &tool, const json &input, const std::string &reason = "");

    // 仅测试用:清空。
    void clearForTests();

    // 快照(单元测试用)。
    std::vector<PermissionRule> snapshot() const;

  private:
    mutable std::mutex mu_;
    std::string path_;
    std::vector<PermissionRule> rules_;

    static std::string resolvePath(const std::string &override);
    // 把 input 序列化成单行字符串(避免 json 内部空白干扰正则)。
    static std::string serializeInput(const json &input);
  };

} // namespace aicoder
