#include "ui/SessionPicker.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace aicoder {

std::optional<std::string> showSessionPicker(SessionStore &store,
                                              const std::string &currentId) {
  auto sessions = store.listSessions();
  if (sessions.empty()) {
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto root = ftxui::Renderer([] {
      return ftxui::vbox({
          ftxui::text("无历史会话") | ftxui::bold,
          ftxui::text("按任意键返回") | ftxui::dim,
      });
    });
    root = ftxui::CatchEvent(root, [&](ftxui::Event) { screen.Exit(); return true; });
    screen.Loop(root);
    return std::nullopt;
  }

  int selected = 0;
  std::optional<std::string> chosen;
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  // 从 SessionInfo 列表重建显示条目（每次删除后调用）。
  auto rebuildEntries = [&] {
    std::vector<std::string> entries;
    entries.reserve(sessions.size());
    for (const auto &s : sessions) {
      std::string row = s.updated_at + "  " + s.model + "  ";
      row += s.preview.empty() ? "(无预览)" : s.preview;
      row += "  [" + std::to_string(s.message_count) + " 条]";
      if (s.id == currentId)
        row += "  ← 当前";
      entries.push_back(std::move(row));
    }
    return entries;
  };

  auto entries = rebuildEntries();
  auto menu = ftxui::Menu(&entries, &selected);

  // 确认删除状态：非 -1 表示正在确认删除 sessions[confirmTarget]。
  int confirmTarget = -1;

  auto root = ftxui::Renderer(menu, [&] {
    auto headerText = confirmTarget >= 0
        ? "确认删除？（y / n）"
        : "↑↓ 移动  Enter 切换  x 删除  Esc 返回";
    auto listView = ftxui::vbox({
        ftxui::text(headerText) | ftxui::bold,
        ftxui::separator(),
        menu->Render() | ftxui::yframe | ftxui::flex,
    }) | ftxui::border;

    if (confirmTarget >= 0) {
      auto &s = sessions[confirmTarget];
      auto label = ftxui::text("确认删除会话 " + s.id + " ?") | ftxui::bold;
      auto hint  = ftxui::text("y — 确认删除    n / Esc — 取消") | ftxui::dim;
      auto dialog = ftxui::vbox({label, ftxui::text(""), hint})
                  | ftxui::border | ftxui::center | ftxui::clear_under;
      return ftxui::dbox({listView, dialog});
    }
    return listView;
  });

  root = ftxui::CatchEvent(root, [&](ftxui::Event e) {
    // ---- 确认删除模式：只响应 y / n / Esc ----
    if (confirmTarget >= 0) {
      if (e == ftxui::Event::Character('y') ||
          e == ftxui::Event::Character('Y')) {
        store.remove(sessions[confirmTarget].id);
        sessions = store.listSessions();
        if (sessions.empty()) { screen.Exit(); return true; }
        entries = rebuildEntries();
        if (selected >= static_cast<int>(entries.size()))
          selected = static_cast<int>(entries.size()) - 1;
        confirmTarget = -1;
        return true;
      }
      if (e == ftxui::Event::Character('n') ||
          e == ftxui::Event::Character('N') ||
          e == ftxui::Event::Escape) {
        confirmTarget = -1;
        return true;
      }
      return true;  // 其它键全吞
    }

    // ---- 正常模式 ----
    if (e == ftxui::Event::Return) {
      if (selected >= 0 && selected < static_cast<int>(sessions.size())) {
        if (sessions[selected].id != currentId)
          chosen = sessions[selected].id;
      }
      screen.Exit();
      return true;
    }
    if (e == ftxui::Event::Escape) { screen.Exit(); return true; }
    // x 键删除（当前会话不可删）
    if (e == ftxui::Event::Character('x') ||
        e == ftxui::Event::Character('X')) {
      if (selected >= 0 && selected < static_cast<int>(sessions.size()) &&
          sessions[selected].id != currentId) {
        confirmTarget = selected;
      }
      return true;
    }
    return false;
  });

  screen.Loop(root);
  return chosen;
}

}  // namespace aicoder
