#pragma once
#include <stdexcept>

namespace aicoder
{
    struct ToolError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
    struct LlmError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
    struct ConfigError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
}
