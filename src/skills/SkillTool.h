#pragma once
#include "core/Tool.h"
#include "skills/SkillRegistry.h"
namespace aicoder {
// reg 必须在工具生命周期内存活（工具按引用捕获）。
Tool makeSkillTool(const SkillRegistry& reg);
}
