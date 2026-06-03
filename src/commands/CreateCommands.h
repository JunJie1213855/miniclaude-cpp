#pragma once
#include <filesystem>
#include <optional>
#include <string>
namespace aicoder
{
struct CreateOutcome
{
  std::filesystem::path path;
  std::string success;
  std::string error;
  std::string name;
  std::string description;
  std::string body;
};
// 将用户输入的 "/create_xxx <args>" 中 <args> 部分解析为 name / description / body。
// body 通过 '|' 分隔(head 是 "name [description]";tail 是 body)。
// 没有 '|' 时 body 为空,由调用方决定是否触发 LLM 生成。
struct CreateArgs
{
  std::string name;
  std::string description;
  std::string body;
};
std::optional<CreateArgs> parseCreateArgs(const std::string &arg);
bool isValidResourceName(const std::string &name);
// 4 个工厂:接收已解析好的字段,只负责"写文件 + 组装 CreateOutcome"。
CreateOutcome createSkill(const std::string &name, const std::string &description,
                          const std::string &body, const std::filesystem::path &globalSkillsDir);
CreateOutcome createCommand(const std::string &name, const std::string &description,
                            const std::string &body, const std::filesystem::path &globalCommandsDir);
CreateOutcome createAgent(const std::string &name, const std::string &description,
                          const std::string &body, const std::filesystem::path &globalAgentsDir);
CreateOutcome createRule(const std::string &name, const std::string &description,
                         const std::string &body, const std::filesystem::path &globalRulesDir);
}
