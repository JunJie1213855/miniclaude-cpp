#pragma once

#include "core/Tool.h"

namespace aicoder {

class SubAgentManager;

Tool makeGetSubAgentResultTool(const SubAgentManager& mgr);

}  // namespace aicoder
