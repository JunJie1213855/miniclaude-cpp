#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace aicoder {
struct SkillInfo { std::string name; std::string description; std::string body; };
class SkillRegistry {
public:
  void discover(const std::filesystem::path& globalDir,
                const std::filesystem::path& projectDir);
  void addOne(const std::filesystem::path& skillMdFile);
  const SkillInfo* find(const std::string& name) const;
  std::vector<SkillInfo> list() const;
  std::string promptList() const;  // "- name: description\n" lines; "" if none
private:
  std::map<std::string, SkillInfo> skills_;
};
}
