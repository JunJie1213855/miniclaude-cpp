#include "PermissionDialog.h"
#include "ftxui/dom/elements.hpp"

namespace aicoder {

PermissionDialog::PermissionDialog(PermissionRequest request)
    : request_(std::move(request)), selected_index_(0), finished_(false), result_(PermissionChoice::Deny) {}

void PermissionDialog::Reset() {
    selected_index_ = 0;
    finished_ = false;
    result_ = PermissionChoice::Deny;
}

void PermissionDialog::SelectNext() {
    selected_index_ = (selected_index_ + 1) % kItemCount;
}

void PermissionDialog::SelectPrev() {
    selected_index_ = (selected_index_ + kItemCount - 1) % kItemCount;
}

void PermissionDialog::Confirm() {
    finished_ = true;
    switch (selected_index_) {
        case 0: result_ = PermissionChoice::Allow; break;
        case 1: result_ = PermissionChoice::AllowForever; break;
        case 2: result_ = PermissionChoice::Deny; break;
        default: result_ = PermissionChoice::Deny; break;
    }
}

void PermissionDialog::Deny() {
    finished_ = true;
    result_ = PermissionChoice::Deny;
}

bool PermissionDialog::OnEvent(ftxui::Event event) {
    if (finished_) return false;

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Tab) {
        SelectNext();
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        SelectPrev();
        return true;
    }
    if (event == ftxui::Event::Return) {
        Confirm();
        return true;
    }
    if (event == ftxui::Event::Escape) {
        Deny();
        return true;
    }
    return false;
}

ftxui::Element PermissionDialog::Render() const {
    using namespace ftxui;

    Elements items;

    // 标题
    items.push_back(hbox({
        text(" Permission Request ") | bold | color(Color::Cyan) | border,
        filler(),
    }));

    // Tool 信息
    items.push_back(hbox({
        text("Tool: ") | bold | color(Color::Yellow),
        text(request_.tool_name) | color(Color::White),
        filler(),
    }));

    // Args 信息
    std::string args_display = request_.arguments;
    if (args_display.size() > 40) {
        args_display = args_display.substr(0, 37) + "...";
    }
    items.push_back(hbox({
        text("Args: ") | bold | color(Color::Yellow),
        text(args_display) | color(Color::White),
        filler(),
    }));

    // What the agent wants to do
    items.push_back(text("What the agent wants to do:") | dim | color(Color::GrayLight));
    items.push_back(text(request_.description) | color(Color::White));

    // 选项
    const char* labels[] = {"Yes", "Yes, and never ask again", "No"};

    for (int i = 0; i < kItemCount; i++) {
        std::string prefix = (i == selected_index_) ? "❯ " : "  ";

        Element item;
        if (i == selected_index_) {
            // 选中项：反色高亮
            item = hbox({
                text(prefix) | color(Color::Black) | bgcolor(Color::Cyan),
                text(labels[i]) | color(Color::Black) | bgcolor(Color::Cyan),
            });
        } else {
            // 未选中项
            item = hbox({
                text(prefix) | color(Color::GrayDark),
                text(labels[i]) | color(Color::White),
            });
        }
        items.push_back(item);
    }

    // 按键提示
    items.push_back(hbox({
        text("  Up/Down: switch  "),
        text("Enter: confirm  "),
        text("Esc: deny"),
    }) | dim | color(Color::GrayDark));

    return vbox(std::move(items)) | border;
}

}
