#include "ui/ResumePicker.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace aicoder {

std::optional<std::string> showResumePicker(const SessionStore& store) {
  auto sessions = store.listSessions();
  if (sessions.empty()) {
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto root = ftxui::Renderer([] {
      return ftxui::vbox({
          ftxui::text("无历史会话") | ftxui::bold,
          ftxui::text("按任意键开始新会话") | ftxui::dim,
      });
    });
    root = ftxui::CatchEvent(root, [&](ftxui::Event) { screen.Exit(); return true; });
    screen.Loop(root);
    return std::nullopt;
  }

  int selected = 0;
  std::optional<std::string> chosen;
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  std::vector<std::string> entries;
  entries.reserve(sessions.size());
  for (const auto& s : sessions) {
    std::string row = s.updated_at + "  " + s.model + "  ";
    row += s.preview.empty() ? "(无预览)" : s.preview;
    row += "  [" + std::to_string(s.message_count) + " 条]";
    entries.push_back(std::move(row));
  }

  auto menu = ftxui::Menu(&entries, &selected);

  auto root = ftxui::Renderer(menu, [&] {
    return ftxui::vbox({
        ftxui::text("选择要恢复的会话（↑↓ 移动，Enter 选中，Esc 新会话）") | ftxui::bold,
        ftxui::separator(),
        menu->Render() | ftxui::yframe | ftxui::flex,
    }) | ftxui::border;
  });

  root = ftxui::CatchEvent(root, [&](ftxui::Event e) {
    if (e == ftxui::Event::Return) {
      if (selected >= 0 && selected < static_cast<int>(sessions.size()))
        chosen = sessions[selected].id;
      screen.Exit();
      return true;
    }
    if (e == ftxui::Event::Escape) { screen.Exit(); return true; }
    return false;
  });

  screen.Loop(root);
  return chosen;
}

}  // namespace aicoder
