#include "PermissionDialog.h"
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

#include <string>
#include <sstream>

namespace aicoder {

PermissionDialog::PermissionDialog(PermissionRequest request)
    : request_(std::move(request)), selected_index_(0), finished_(false), result_(PermissionChoice::Deny) {
  // 三个 Yes/No 选项作为 ftxui::Menu 子组件。
  // 关键:Add(menu_) 把子组件挂进 component 树,这样它:
  //   1) 接收焦点(input 给本 Dialog,本 Dialog 透传给 Menu)
  //   2) 正确计算高度(Menu 的 Render() 输出多行)
  //   3) OnEvent 内部自动转发 ↑/↓/Enter,无需自己手动处理
  menuEntries_ = {"Yes", "Yes, and never ask again", "No"};
  ftxui::MenuOption opt = ftxui::MenuOption::Vertical();
  // Enter 由 Menu 内部处理:触发 on_enter → 我们这里调 Confirm()
  // 把 selected_index_ 翻译成 PermissionChoice + finished_=true。
  opt.on_enter = [this]() { Confirm(); };
  menu_ = ftxui::Menu(&menuEntries_, &selected_index_, opt);
  Add(menu_);
}

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

    // Esc 永远意味着"拒绝",先处理(不被 Menu 吞掉)。
    if (event == ftxui::Event::Escape) {
        Deny();
        return true;
    }

    // 其它事件先让 Menu 处理(↑/↓ 切选项,Enter 确认)。
    // Menu 的 OnEvent 会自己维护 selected_index_;我们 OnRender 时
    // 直接读它,所以无重复状态。
    if (menu_ && menu_->OnEvent(event))
      return true;

    // 兜底:Enter 没被 Menu 吃(应该不会)就 Confirm 一次。
    if (event == ftxui::Event::Return) {
        Confirm();
        return true;
    }
    return false;
}

namespace {

// 单行短缩略:超过 limit 字节就 "…<truncated N chars>"。避免把几 KB 源码塞 UI。
std::string truncateOneLine(const std::string& s, size_t limit) {
    if (s.size() <= limit) return s;
    std::stringstream out;
    out << "<truncated " << s.size() << " chars> " << s.substr(0, limit) << "…";
    return out.str();
}

// 缩进多行(每行前缀 4 空格),让嵌套在 layout 里不会顶到左边框。
std::string indentMultiline(const std::string& s, const std::string& pad) {
    std::string out;
    out.reserve(s.size() + pad.size() * 4);
    bool atLineStart = true;
    for (char c : s) {
        if (atLineStart) out += pad;
        out += c;
        atLineStart = (c == '\n');
    }
    return out;
}

}  // namespace

std::string PermissionDialog::formatArgs() const {
    // 命令行类(以 "Bash" 命名约定的): 拼出可读命令行
    if (request_.tool_name == "Bash" || request_.tool_name == "bash" || request_.tool_name == "Shell") {
        if (request_.input.is_object()) {
            std::string cmd = request_.input.value("command", std::string{});
            if (cmd.empty()) cmd = request_.input.value("cmd", std::string{});
            return truncateOneLine(cmd, 240);
        }
        if (request_.input.is_string()) return request_.input.get<std::string>();
        return request_.input.dump();
    }
    // 文件类工具:渲染 path + content(若 content 过长则截断)
    if (request_.tool_name == "WriteFile" || request_.tool_name == "write_file" ||
        request_.tool_name == "EditFile"  || request_.tool_name == "edit_file"  ||
        request_.tool_name == "ReadFile"  || request_.tool_name == "read_file"  ||
        request_.tool_name == "DeleteFile"|| request_.tool_name == "delete_file") {
        if (request_.input.is_object()) {
            std::string path = request_.input.value("path", std::string{});
            std::string content = request_.input.value("content", std::string{});
            std::stringstream out;
            out << "path: \"" << path << "\"";
            if (!content.empty()) {
                out << "\ncontent: ";
                if (content.size() <= kContentTruncateBytes) {
                    out << "\"" << content << "\"";
                } else {
                    out << "\"" << content.substr(0, kContentTruncateBytes)
                        << "…<truncated " << content.size() << " chars total>\"";
                }
            }
            return indentMultiline(out.str(), "    ");
        }
    }
    // 兜底:序列化整个 json(若是字符串、单值直接显示)
    if (request_.input.is_string()) return truncateOneLine(request_.input.get<std::string>(), 240);
    if (request_.input.is_null()) return "(no args)";
    return request_.input.dump();
}

ftxui::Element PermissionDialog::OnRender() {
    using namespace ftxui;

    // 工具/参数 —— 走 formatArgs() 按 tool_name 格式化截断
    std::string args_str = formatArgs();
    // 把多行 args 拆开,每行一个 element
    Elements argsLines;
    {
        std::stringstream ss(args_str);
        std::string line;
        bool first = true;
        while (std::getline(ss, line)) {
            if (first) {
                argsLines.push_back(hbox({
                    text(" * args: ") | color(Color::Yellow),
                    text(line) | color(Color::White),
                }));
                first = false;
            } else {
                // 续行:10 空格缩进对齐 " * args: " 之后的列
                argsLines.push_back(hbox({
                    text("          ") | color(Color::Yellow),
                    text(line) | color(Color::White),
                }));
            }
        }
    }

    // 主结构:tool 行 + args 行 + 间隔 + Menu 渲染 + 键位提示
    // 注意:menu_->Render() 必须显式调用 — 它是子组件,只有 Add+Render
    // 才会画出多行 + 计算正确高度。
    Elements content;
    content.push_back(hbox({
        text(" * tool: ") | color(Color::Yellow),
        text(request_.tool_name) | bold | color(Color::White),
    }) | xflex);
    for (auto& e : argsLines) content.push_back(e | xflex);
    content.push_back(text(""));
    content.push_back(menu_->Render() | xflex);
    content.push_back(text(""));
    content.push_back(hbox({
        text("  ↑/↓ 切换  ") | dim,
        text("Enter 确认  ") | dim,
        text("Esc 拒绝") | dim,
    }) | xflex);

    // 边框 + 标题:用 ftxui::window 给一个带标题的容器,效果等同于
    // "┌─ Permission Request ─...─┐" 框,且不强制固定高度 — 自适应内容。
    return window(text(" Permission Request ") | bold | color(Color::Cyan),
                  vbox(std::move(content)) | xflex) |
           xflex;
}

}
