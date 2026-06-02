#pragma once
#include "core/ToolRegistry.h"

namespace aicoder
{
    // 注册所有内置工具到 registry（单一来源，供 main.cpp / main_tui.cpp 共用，避免两处重复）。
    void registerBuiltinTools(ToolRegistry &registry);
}
