#include "skills/SkillTool.h"
#include "core/Errors.h"
namespace aicoder {
Tool makeSkillTool(const SkillRegistry& reg) {
  Tool t;
  t.name = "skill";
  t.description =
      "Load a skill: returns the skill's markdown instructions to follow. "
      "Use when a task matches an available skill (see the system prompt's 可用技能 list).";
  json nameSchema = {{"type", "string"}, {"description", "Skill name"}};
  json names = json::array();
  for (const auto& s : reg.list()) names.push_back(s.name);
  if (!names.empty()) nameSchema["enum"] = names;  // 无技能时省略 enum，避免空枚举
  t.input_schema = json{
      {"type", "object"},
      {"properties", {{"name", nameSchema}}},
      {"required", json::array({"name"})}};
  t.needsPermission = false;
  t.execute = [&reg](const json& input) -> std::string {
    if (!input.contains("name") || !input["name"].is_string())
      throw ToolError("missing skill name");
    std::string name = input["name"].get<std::string>();
    const SkillInfo* s = reg.find(name);
    if (!s) throw ToolError("unknown skill: " + name);
    return s->body;
  };
  return t;
}
}
