#pragma once
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/event.hpp"
#include <string>
#include "core/Json.h"

namespace aicoder {

enum class PermissionChoice {
    Allow,
    AllowForever,
    Deny
};

struct PermissionRequest {
    std::string tool_name;     // 工具函数名（"Bash" / "WriteFile" / ...）— 必须，绝不能是文件名
    json input;                // 工具参数 — 由 Render 按工具名格式化/截断
    std::string description;   // 助手给的 "what it intends to do"
};

class PermissionDialog {
public:
    explicit PermissionDialog(PermissionRequest request);

    ftxui::Element Render() const;
    bool OnEvent(ftxui::Event event);

    bool Finished() const { return finished_; }
    PermissionChoice Result() const { return result_; }
    void Reset();

    void SelectNext();
    void SelectPrev();
    void Confirm();
    void Deny();

private:
    // 把 input 渲染成对用户友好的多行字符串(短命令直接显示,长内容截断成
    // "path: "..." \n content: <truncated 500 chars>" 这种键值形式)。
    // 绝不能把整段 HTML 原文塞进 UI。
    std::string formatArgs() const;

    PermissionRequest request_;
    int selected_index_ = 0;
    bool finished_ = false;
    PermissionChoice result_ = PermissionChoice::Deny;
    static constexpr int kItemCount = 3;
    static constexpr size_t kContentTruncateBytes = 500;
};

}
