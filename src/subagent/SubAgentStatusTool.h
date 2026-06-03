#pragma once

#include "core/Tool.h"

namespace aicoder {

class SubAgentManager;

Tool makeGetSubAgentStatusTool(const SubAgentManager& mgr);

}  // namespace aicoder
