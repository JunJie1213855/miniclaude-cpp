#pragma once
#include <optional>
#include <string>
#include "sessions/SessionStore.h"

namespace aicoder
{
    // TUI 运行期间弹出全屏会话列表。
    // currentId: 高亮标记当前会话（显示 "← 当前"），不可删除。
    // 按 x 删除选中会话（需确认），Enter 切换，Esc 返回。
    // 返回用户选中的 session id，或 nullopt（Esc / 空列表 / 选当前会话）。
    std::optional<std::string> showSessionPicker(SessionStore &store,
                                                  const std::string &currentId);
}
