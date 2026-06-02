#pragma once
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/event.hpp"
#include <string>
#include <vector>
#include "core/Json.h"

namespace aicoder {

enum class PermissionChoice {
    Allow,
    AllowForever,
    Deny
};

struct PermissionRequest {
    std::string tool_name;     // 工具函数名("Bash" / "WriteFile" / ...)— 必须,绝不能是文件名
    json input;                // 工具参数 — 由 Render 按工具名格式化/截断
    std::string description;   // 助手给的 "what it intends to do"
};

// 升级为 ftxui::ComponentBase —— 这样 dialog 自身能被加到
// Component 树里(focus / Maybe 互斥 / OnEvent 路由)而不是裸 Element。
// 此前只是 Element,事件路由靠外层 CatchEvent 手工转发 —— 焦点拿不到。
class PermissionDialog : public ftxui::ComponentBase {
public:
    explicit PermissionDialog(PermissionRequest request);

    // ftxui::ComponentBase overrides
    // 注意:基类 Render() 非虚,虚的是 OnRender() —— 我们的渲染逻辑放 OnRender
    // 即可,外层用 ->Render() 调用仍能拿到 Element。
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;

    bool Finished() const { return finished_; }
    PermissionChoice Result() const { return result_; }
    void Reset();

    void SelectNext();
    void SelectPrev();
    void Confirm();
    void Deny();

private:
    std::string formatArgs() const;

    PermissionRequest request_;
    int selected_index_ = 0;
    bool finished_ = false;
    PermissionChoice result_ = PermissionChoice::Deny;
    static constexpr int kItemCount = 3;
    static constexpr size_t kContentTruncateBytes = 500;

    // 菜单子组件 —— 必须在 ctor 中 Add 进组件树,这样它能接收焦点、
    // 计算正确高度、并由 OnEvent 自动转发键盘事件。
    ftxui::Component menu_;
    // 菜单的三个选项(Menu 要 std::vector<std::string>)。
    std::vector<std::string> menuEntries_;
};

}  // namespace aicoder
