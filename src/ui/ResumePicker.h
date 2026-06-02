#pragma once
#include <optional>
#include <string>
#include "sessions/SessionStore.h"

namespace aicoder
{
    // TUI 启动时弹出全屏会话列表，选择要恢复的会话。
    // currentId: 高亮标记当前会话（显示 "← 当前"），不可删除。
    //           启动时尚未有当前会话，默认为空。
    // 按 x 删除选中会话（需确认），Enter 恢复，Esc 新会话。
    // 返回用户选中的 session id，或 nullopt（Esc / 空列表 / 选当前会话）。
    std::optional<std::string> showResumePicker(SessionStore &store,
                                                const std::string &currentId = "");
}
