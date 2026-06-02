#include "Welcome.h"

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"

using namespace ftxui;

namespace aicoder
{

    // AI 核心/芯片像素图（带引脚的处理器）
    // 颜色：棕色基调，与原吉祥物风格一致。
    static const std::vector<std::string> PIXEL_ART = {
        "                  ┌───┐                    ",
        "                ──┤   ├──                  ",
        "               ===│ o │===                 ",
        "                ──┤   ├──                  ",
        "                  └───┘                    ",
    };

    ftxui::Element welcomeScreen()
    {
        using namespace ftxui;

        // Left column: welcome text + AI chip pixel art + status
        Elements leftCol;
        leftCol.push_back(text("Welcome back!") | bold | color(Color::Cyan) | center);
        leftCol.push_back(text(""));
        for (const auto &line : PIXEL_ART)
        {
            leftCol.push_back(text(line) | color(Color::RGB(200, 140, 100)) | center);
        }
        leftCol.push_back(text(""));
        leftCol.push_back(text("MiniMax-M3 • Local Agent Task") | dim | center);
        leftCol.push_back(text("~/build/linux/x86_64/aicoder") | dim | center);

        // Right column: tips panel + What's new
        Elements rightCol;
        rightCol.push_back(text("Tips for getting started") | bold | color(Color::Yellow));
        rightCol.push_back(text("Run /init to create an AICODER.md file with"));
        rightCol.push_back(text("instructions for AICoder") | dim);
        rightCol.push_back(text(""));
        rightCol.push_back(text(" ─────────────────────────────────── ") | dim);
        rightCol.push_back(text("What's new") | bold);
        rightCol.push_back(text("• Supercharged C++ build support via xmake") | dim);
        rightCol.push_back(text("• Smart layout caching for multi-agent workflows") | dim);
        rightCol.push_back(text("• Fixed static/dynamic library size recaps") | dim);
        rightCol.push_back(text(""));
        rightCol.push_back(text("/release-notes for more") | dim);

        Element left = vbox(std::move(leftCol)) | flex;
        Element right = vbox(std::move(rightCol)) | flex;

        Element content = hbox({left | size(WIDTH, LESS_THAN, 45), separator(), right | flex});

        // Title bar — 整张欢迎页最顶端一行,自带 "┌─ ... ─┐" 边框,
        // 后面 vbox 不要再加外层 | border,否则会把这一行再包一次,出现
        // "标题 + 一整圈外框" 的双层效果。
        // 宽度自适应:取父容器宽度减 1(因为手动画了 '┐')。
        Element title = hbox({
                            text(" ┌─ AICoder v1.0.256 "),
                            filler(),
                            text("┐"),
                        }) |
                        color(Color::Cyan);

        // 底部边框 "└─...─┘" 由内层 hbox 一起提供,使整个欢迎页呈一个完整方框
        Element bottom = hbox({
                             text(" └"),
                             filler() | dim,
                             text("┘"),
                         }) |
                         color(Color::Cyan);

        return vbox({
                   title,
                   content | flex,
                   bottom,
               });
    }

} // namespace aicoder
