#pragma once
#include <optional>
#include <string>
#include "sessions/SessionStore.h"

namespace aicoder
{
    // Show full-screen list of sessions; returns selected id or nullopt (Esc/empty list).
    std::optional<std::string> showResumePicker(const SessionStore &store);
}
