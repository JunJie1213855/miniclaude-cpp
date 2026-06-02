#include "Welcome.h"

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"

using namespace ftxui;

namespace aicoder {

// Simplified pixel art: cute brown/pink face (16 lines x ~32 chars)
// Colors: browns, pinks, grays for the cute face
static const std::vector<std::string> PIXEL_ART = {
    "                                        ",
    "      ██████████████████████████        ",
    "    ████░░░░░░░░░░░░░░░░░░░░░░░░████  ",
    "  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "  ████░░░░  ████████  ████████  ░░████",
    "  ████░░░░  ████████  ████████  ░░████",
    "  ████░░░░  ████░██░  ██░████░  ░░████",
    "  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "  ████░░░░░░  ████  ████  ░░░░░░░░████",
    "  ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "    ████░░░░░░░░░░░░░░░░░░░░░░░░░░████",
    "      ████████████████████████████░░    ",
    "                                        ",
};

ftxui::Element welcomeScreen() {
  using namespace ftxui;

  // Left column: pixel art + welcome text
  Elements leftCol;
  for (const auto& line : PIXEL_ART) {
    leftCol.push_back(text(line) | color(Color::RGB(200, 140, 100)));
  }
  leftCol.push_back(text(""));
  leftCol.push_back(text("Welcome back!") | bold | color(Color::Cyan) | center);
  leftCol.push_back(text("MiniMax-M2.7 • Local Agent Task") | dim | center);
  leftCol.push_back(text("~/build/linux/x86_64/aicoder") | dim | center);

  // Right column: tips panel
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
  rightCol.push_back(text("/release-notes for more") | dim);

  Element left = vbox(std::move(leftCol)) | flex;
  Element right = vbox(std::move(rightCol)) | flex;

  Element content = hbox({left | size(WIDTH, LESS_THAN, 45), separator(), right | flex});

  // Title bar
  Element title = hbox({
      text(" ┌ AICoder v0.1.0 "),
      text("─────────────────────────────────────────────────────────────────── ") | dim,
  }) | color(Color::Cyan);

  return vbox({
             title,
             content | flex,
         }) |
         border;
}

}  // namespace aicoder
