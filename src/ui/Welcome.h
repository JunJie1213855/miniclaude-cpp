#pragma once

#include <string>
#include <vector>
#include "ftxui/component/component.hpp"

namespace aicoder
{

    // Welcome screen with ASCII pixel art representation.
    // model: 当前使用的模型名，显示在欢迎页中。
    ftxui::Element welcomeScreen(const std::string& model);

} // namespace aicoder
