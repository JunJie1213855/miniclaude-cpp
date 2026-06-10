// 回归测试:ReplView 中权限弹窗事件链。覆盖 default Yes、shift to AllowForever、shift to Deny 路径。
#include "ui/PermissionDialog.h"
#include "core/Json.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <gtest/gtest.h>
#include <memory>

using namespace aicoder;
using namespace ftxui;

namespace {

// 模拟 ReplView L965 的 CatchEvent + L1153 的 Maybe(dialog, true)。
TEST(PermissionDialogEnterFlow, ReturnFinishesDialogWhenMaybeShowTrue) {
  bool pending = true;
  auto dialogRaw = Make<PermissionDialog>(PermissionRequest{
      "Bash",
      json{{"command", "ls"}},
      "list"});
  auto maybe = Maybe(dialogRaw, [&] { return pending; });

  bool handled = maybe->OnEvent(Event::Return);
  EXPECT_TRUE(handled);

  auto dlg = std::dynamic_pointer_cast<PermissionDialog>(dialogRaw);
  ASSERT_NE(dlg, nullptr);
  EXPECT_TRUE(dlg->Finished());
  EXPECT_EQ(dlg->Result(), PermissionChoice::Allow);
}

TEST(PermissionDialogEnterFlow, ReturnDoesNotFinishDialogWhenMaybeShowFalse) {
  bool pending = false;
  auto dialogRaw = Make<PermissionDialog>(PermissionRequest{
      "Bash",
      json{{"command", "ls"}},
      "list"});
  auto maybe = Maybe(dialogRaw, [&] { return pending; });

  bool handled = maybe->OnEvent(Event::Return);
  EXPECT_FALSE(handled);

  auto dlg = std::dynamic_pointer_cast<PermissionDialog>(dialogRaw);
  ASSERT_NE(dlg, nullptr);
  EXPECT_FALSE(dlg->Finished());
}

// 模拟 ReplView 的完整事件路由:Renderer(Maybe(dialog)) + CatchEvent
TEST(PermissionDialogEnterFlow, FullReplViewLikeChain) {
  bool pending = true;
  bool grantCalled = false;
  PermissionChoice granted = PermissionChoice::Deny;

  auto dialogRaw = Make<PermissionDialog>(PermissionRequest{
      "Bash",
      json{{"command", "ls"}},
      "list"});
  auto maybe = Maybe(dialogRaw, [&] { return pending; });

  // Renderer 包 maybe —— 等价于 ReplView L789
  auto inner = Renderer([&] { return text("dummy"); });
  inner->Add(maybe);

  // CatchEvent 包 inner —— 等价于 ReplView L965
  // 复制 ReplView L967-986 的 lambda
  auto top = CatchEvent(inner, [&](Event e) {
    if (pending && maybe) {
      if (maybe->OnEvent(e)) {
        auto dlg = std::dynamic_pointer_cast<PermissionDialog>(dialogRaw);
        if (dlg && dlg->Finished()) {
          granted = dlg->Result();
          grantCalled = true;
          pending = false;
        }
        return true;
      }
      return true;
    }
    return false;
  });

  bool handled = top->OnEvent(Event::Return);
  EXPECT_TRUE(handled);
  EXPECT_TRUE(grantCalled);
  EXPECT_EQ(granted, PermissionChoice::Allow);
}

// 回归:用户先用 ArrowDown 切到 "Yes, and never ask again",再按 Enter。
// bug 报告:这个路径下 Enter 卡住且权限未授权成功。
TEST(PermissionDialogEnterFlow, ArrowDownThenReturnGrantsAllowForever) {
  bool pending = true;
  auto dialogRaw = Make<PermissionDialog>(PermissionRequest{
      "Bash",
      json{{"command", "cmake -S . -B build"}},
      "build"});

  auto maybe = Maybe(dialogRaw, [&] { return pending; });
  auto inner = Renderer([&] { return text("dummy"); });
  inner->Add(maybe);

  bool grantCalled = false;
  PermissionChoice granted = PermissionChoice::Deny;
  auto top = CatchEvent(inner, [&](Event e) {
    if (pending && maybe) {
      if (maybe->OnEvent(e)) {
        auto dlg = std::dynamic_pointer_cast<PermissionDialog>(dialogRaw);
        if (dlg && dlg->Finished()) {
          granted = dlg->Result();
          grantCalled = true;
          pending = false;
        }
        return true;
      }
      return true;
    }
    return false;
  });

  // 模拟用户操作:ArrowDown → Return
  bool h1 = top->OnEvent(Event::ArrowDown);
  EXPECT_TRUE(h1) << "ArrowDown should be consumed";
  bool h2 = top->OnEvent(Event::Return);
  EXPECT_TRUE(h2) << "Return should be consumed";

  EXPECT_TRUE(grantCalled) << "grantPermission should have been triggered";
  EXPECT_EQ(granted, PermissionChoice::AllowForever);
}

// 回归:ArrowDown x2 → 切到 No (Deny),再按 Enter。
TEST(PermissionDialogEnterFlow, ArrowDownTwiceThenReturnDenies) {
  bool pending = true;
  auto dialogRaw = Make<PermissionDialog>(PermissionRequest{
      "Bash",
      json{{"command", "rm -rf /"}},
      "danger"});

  auto maybe = Maybe(dialogRaw, [&] { return pending; });
  auto inner = Renderer([&] { return text("dummy"); });
  inner->Add(maybe);

  bool grantCalled = false;
  PermissionChoice granted = PermissionChoice::Allow;  // 默认 Allow,等被改
  auto top = CatchEvent(inner, [&](Event e) {
    if (pending && maybe) {
      if (maybe->OnEvent(e)) {
        auto dlg = std::dynamic_pointer_cast<PermissionDialog>(dialogRaw);
        if (dlg && dlg->Finished()) {
          granted = dlg->Result();
          grantCalled = true;
          pending = false;
        }
        return true;
      }
      return true;
    }
    return false;
  });

  bool h1 = top->OnEvent(Event::ArrowDown);
  EXPECT_TRUE(h1);
  bool h2 = top->OnEvent(Event::ArrowDown);
  EXPECT_TRUE(h2);
  bool h3 = top->OnEvent(Event::Return);
  EXPECT_TRUE(h3);

  EXPECT_TRUE(grantCalled);
  EXPECT_EQ(granted, PermissionChoice::Deny);
}

}  // namespace