#pragma once

#include "core/Tool.h"

namespace aicoder {

class SubAgentRegistry;
class SubAgentManager;

Tool makeSubAgentTool(const SubAgentRegistry& reg, SubAgentManager& mgr);

}  // namespace aicoder
