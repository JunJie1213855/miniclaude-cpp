#include "PermissionDialog.h"
#include "ftxui/dom/elements.hpp"

#include <string>
#include <sstream>

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

ftxui::Element PermissionDialog::Render() const {
    using namespace ftxui;

    Elements items;

    // 标题行 —— 严格按设计文档 "┌─ Permission Request ─────┐"
    items.push_back(hbox({
        text("┌─ Permission Request "),
        filler(),
        text("┐"),
    }) | bold | color(Color::Cyan));

    // tool 行 —— " * tool: Bash" 形式,绝不能是文件名
    items.push_back(hbox({
        text(" * tool: ") | color(Color::Yellow),
        text(request_.tool_name) | bold | color(Color::White),
        filler(),
    }));

    // args 行(可多行)—— 按工具名格式化
    std::string args_str = formatArgs();
    {
        std::stringstream ss(args_str);
        std::string line;
        bool first = true;
        while (std::getline(ss, line)) {
            if (first) {
                items.push_back(hbox({
                    text(" * args: ") | color(Color::Yellow),
                    text(line) | color(Color::White),
                    filler(),
                }));
                first = false;
            } else {
                items.push_back(hbox({
                    text("          ") | color(Color::Yellow),  // 对齐 " * args: "
                    text(line) | color(Color::White),
                    filler(),
                }));
            }
        }
    }

    items.push_back(text(""));

    // 3 个选项:❯ Yes / Yes, and never ask again / No
    const char* labels[] = {"Yes", "Yes, and never ask again", "No"};
    for (int i = 0; i < kItemCount; i++) {
        std::string prefix = (i == selected_index_) ? "❯ " : "  ";
        Element item;
        if (i == selected_index_) {
            item = hbox({
                text(prefix) | color(Color::Black) | bgcolor(Color::Cyan),
                text(labels[i]) | color(Color::Black) | bgcolor(Color::Cyan),
            });
        } else {
            item = hbox({
                text(prefix) | color(Color::GrayDark),
                text(labels[i]) | color(Color::White),
            });
        }
        items.push_back(item);
    }

    // 底部分隔
    items.push_back(hbox({
        text("└"),
        filler(),
        text("┘"),
    }) | color(Color::Cyan));

    // 键位提示
    items.push_back(text(""));
    items.push_back(hbox({
        text("  ↑/↓ 切换  ") | dim,
        text("Enter 确认  ") | dim,
        text("Esc 拒绝") | dim,
    }));

    return vbox(std::move(items)) | border;
}

}
