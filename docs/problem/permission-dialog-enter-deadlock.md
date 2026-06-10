# 权限弹窗 Enter 键死锁问题

## 现象

用户触发需要权限确认的工具（如 `bash`、`write_file`），TUI 弹出三选一权限对话框（Yes / Yes & Never Ask Again / No）。用户按 `Enter` 选择其中任意一个选项后，界面卡死不动，程序无法恢复。

定位发现 worker 线程永久阻塞在 `ReplView::Impl::waitForPermission` 中的：

```cpp
permissionCv_.wait(lock, [this] { return !permissionPending_; });
```

`permissionPending_` 始终为 `true`，`condition_variable` 永远收不到 `notify`。

## 根因分析

### 事件链路

用户按 `Enter` 后的完整调用链：

```
FTXUI event loop
  → CatchEvent 回调 (ReplView.cpp:966)
  → 检查 permissionPending_ && permissionDialogRaw_ (均为 true)
  → permissionDialogRaw_->OnEvent(Event::Return)     ← 入口
      → PermissionDialog::OnEvent (PermissionDialog.cpp:56)
          → menu_->OnEvent(event)                     ← 关键点
```

### 根本原因

`menu_` 是 `PermissionDialog` 构造函数中通过 `Add(menu_)` 挂入组件树的 FTXUI `Menu` 子组件。它依赖父组件的焦点链 (focus chain) 来正确处理 `Enter` 事件。

但 `waitForPermission` 在 UI 线程执行了组件树动态重组：

```cpp
// ReplView.cpp:1192-1195
inputArea_->DetachAllChildren();                     // 拆光所有子组件
inputArea_->Add(Maybe(this->input, ...));            // 重新挂 input
inputArea_->Add(permissionComponent_);                // 挂 Maybe(dialog)
```

`DetachAllChildren()` + 重新 `Add` 重建了 `inputArea_` 的组件树。虽然 `permissionDialogRaw_` 的 `shared_ptr` 仍然有效，但它内部的 `menu_` 子组件所在的父容器已被动态重建，FTXUI 的焦点通知 (`OnFocus` / `OnActiveChild`) 没有传播到重建后的 `menu_`。

结果：

- `menu_->OnEvent(Event::Return)` 返回 `false`（menu 没有焦点，不处理 Enter）
- `PermissionDialog::OnEvent` 中 `if (menu_ && menu_->OnEvent(event))` 分支不命中
- 兜底 `if (event == ftxui::Event::Return) { Confirm(); }` 也不命中（event 已被 `OnEvent` 消费但未设置 `finished_`）
- `Confirm()` 永远不会被调用
- `finished_` 永远为 `false`
- `CatchEvent` 中的 `grantPermission()` 永远不会执行
- `permissionCv_.notify_one()` 永远不会发出

### 为什么方向键没影响？

`↑` / `↓` 只改变 Menu 的高亮项，不需要焦点即可正常工作。只有 `Enter` 需要触发 `on_enter` 回调，而 FTXUI Menu 的 Enter 处理依赖组件焦点。

## 解决方法

在 `CatchEvent` 回调中直接拦截 `Enter` / `Esc`，绕过 `PermissionDialog` 内部的 `Menu::OnEvent` 链路。

### 修改文件

`src/ui/ReplView.cpp` 第 966-1000 行

### 修改前

```cpp
if (permissionPending_ && permissionDialogRaw_) {
    if (permissionDialogRaw_->OnEvent(e)) {         // 依赖 dialog 内部 Menu
        if (auto dlg = std::dynamic_pointer_cast<PermissionDialog>(permissionDialogRaw_)) {
            if (dlg->Finished()) {
                grantPermission(dlg->Result());
                permissionComponent_.reset();
                permissionDialogRaw_.reset();
            }
        }
        return true;
    }
    return true;
}
```

### 修改后

```cpp
if (permissionPending_ && permissionDialogRaw_) {
    auto dlg = std::dynamic_pointer_cast<PermissionDialog>(permissionDialogRaw_);
    if (dlg) {
        // Enter/Esc 直接分派到 dialog，不经过 Menu::OnEvent(焦点丢失后返回 false)
        if (e == ftxui::Event::Return) {
            dlg->Confirm();
            grantPermission(dlg->Result());
            permissionComponent_.reset();
            permissionDialogRaw_.reset();
            return true;
        }
        if (e == ftxui::Event::Escape) {
            dlg->Deny();
            grantPermission(dlg->Result());
            permissionComponent_.reset();
            permissionDialogRaw_.reset();
            return true;
        }
    }
    // ↑/↓ 等导航键仍走 dialog 内部的 Menu OnEvent（不需要焦点，正常工作）
    if (permissionDialogRaw_ && permissionDialogRaw_->OnEvent(e)) {
        if (dlg && dlg->Finished()) {
            grantPermission(dlg->Result());
            permissionComponent_.reset();
            permissionDialogRaw_.reset();
        }
        return true;
    }
    return true;
}
```

### 为什么这样修复？

1. `Enter` / `Esc` 不再依赖 Menu 子组件的焦点状态
2. 直接调用 `dlg->Confirm()` / `dlg->Deny()` 设置 `finished_=true`
3. 立刻调用 `grantPermission()` 唤醒 worker 线程
4. `↑` / `↓` 导航键仍走原来的 `OnEvent` 链路（它们不需要焦点，正常工作）

## 总结

| 维度 | 内容 |
|------|------|
| **分类** | FTXUI 组件树动态重组导致的焦点丢失 |
| **症状** | Enter 键无响应，worker 线程 `cv.wait` 永久阻塞 |
| **根因** | `DetachAllChildren` + `Add` 重建组件树后，嵌套 Menu 子组件失去焦点，`OnEvent(Return)` 不处理 Enter |
| **修复思路** | 绕过 Menu 的 OnEvent 链路，在 CatchEvent 直接分派 Enter/Esc |
| **影响范围** | `ReplView.cpp` CatchEvent 回调 ~35 行 |
| **测试** | 221 个 GoogleTest 全部通过 |
