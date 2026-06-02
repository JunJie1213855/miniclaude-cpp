# Prompt: 实现 Tool Use Interceptor（工具调用拦截器）

> 把 `toolcall_design.md` 折叠为可复用的结构化 prompt。把它直接喂给一个
> 实现型 agent 即可获得完整方案；下游再按本文档末尾"交付物清单"校对。

---

## 角色设定

你是一位资深的 AI Agent 框架架构师和 TUI（终端用户界面）交互专家。你需要为
一个 C++ 的 Agent 系统（FTXUI 6.1.9、nlohmann/json、c++20、CMake/xmake）
设计并实现一个健壮的 **Tool Use Interceptor（工具调用拦截器）**。

---

## 核心任务

实现一个能够**拦截、排队、校验权限并顺序执行** Tool Calls 的拦截器。当遇到
敏感或需要授权的工具调用时，必须通过 TUI **阻塞主线程**并询问用户；用户授权
后，再继续执行。

---

## 输入契约

调用方（LLM 客户端解析 `assistant.tool_use` 块后）把若干 `ToolUseBlock`
传递给拦截器。每个块包含 `id`、`name`、`input`（nlohmann::json）。

工具通过 `ToolRegistry` 注册，工具的元数据 `Tool::needsPermission` 表明是否
需要权限。

拦截器输出 N 个 `ToolResultBlock`（`id`、`content`、`is_error`）以及一个
"执行被用户拒绝"或"被前置工具失败阻断"的标记——若任一工具被拒/失败，且它
之后的工具有上下文依赖，整个队列的后续工具也要中止并打上 `is_error=true`、
content=`"skipped: previous tool was denied/failed"`。

---

## 详细技术规格

### 1. 队列与依赖性管理

- **顺序执行**：LLM 一次性返回的多个 `ToolUseBlock` 进入队列，**严格按顺序**
  逐个处理（前一个完成、得到结果后才执行下一个）。
- **依赖中断逻辑**：常见依赖如 `bash mkdir` → `write_file`。当队列中某工
  具因用户拒绝（`No`）或执行报错（`ToolError`）而失败时，**立即终止队列**
  ——后续工具不再询问权限、不再执行，直接回写
  `ToolResultBlock{is_error=true, content="skipped: previous tool ... was denied/failed"}`。
  让 LLM 下一轮看到这一串错误，自行决定重试或放弃。

### 2. 权限校验与持久化

- **免密白名单**：工具声明 `needsPermission=false` → 直接执行。
- **拦截触发**：`needsPermission=true` 且**未在持久化白名单中** → 弹窗询问。
- **状态持久化**：
  - 用户选 `Yes, and never ask again` → 把规则写入
    `~/.config/aicoder/permissions.json`（路径可用 `std::filesystem` 取 $XDG_CONFIG_HOME
    兜底 `~/.config`）。
  - 规则 schema：`{"tool": "Bash", "pattern": "^cmake\\s" }` 或
    `{"tool": "WriteFile", "path_regex": "src/.*\\.cpp$"}`。
  - 命中规则 → 静默放行（不进弹窗、不刷日志）。
  - 启动时读一次该文件到内存 map；写入时 fsync 一次再 reload。

### 3. TUI 界面（严格规范）

弹窗必须严格按下面这个 ASCII 形状渲染：

```
┌─ Permission Request ─────────────────────────────┐
│ * tool: Bash                                     │
│ * args: cmake -S . -B build                      │
│                                                  │
│ ❯ Yes                                            │
│   Yes, and never ask again                       │
│   No                                             │
└──────────────────────────────────────────────────┘
```

**防踩坑（必读）**：

- `tool` 字段必须是**工具函数名**（`Bash`、`WriteFile`），**绝不能显示成文件
  名**（如 `snake.html`）。常见 bug：把第一个参数当 tool name 渲染。
- `args` 字段必须**格式化并截断**：
  - 命令行类（`bash`）：显示完整命令字符串（`cmake -S . -B build`）。
  - 文件类（`write_file`）：`path: "xxx"`, `content: "<truncated 500 chars>"`
    这种键值形式，**不能**把几百行 HTML 塞进 UI。
- **全局焦点接管**（关键）：弹窗出现时是阻塞式 Modal。此时**必须隐藏或禁
  用底部的全局聊天输入框**，把方向键/回车键完全绑定到 Yes/No 菜单，避免
  UI 状态冲突。

### 4. Async/Await 阻塞

- TUI 主线程跑 `screen->Loop()`，worker 线程在 `AgentLoop::run` 里
  调 `permCb(toolName, input)`。
- 弹窗代码通过 `screen->Post(...)` 投递 UI 更新 + `screen->PostEvent(Event::Custom)`
  令帧失效；用 `std::mutex` + `std::condition_variable` 让 worker 等待用户
  在 TUI 线程中选 `Allow/AllowForever/Deny`。
- 选定后 `cv.notify_one()`，worker 拿到 `PermissionChoice`，继续/中止队列。

---

## 实施工作流

按下列顺序走，每步小步提交（避免大爆炸 diff）：

1. **prompt 归档**：把这份规格落到 `docs/prompts/permission_interceptor.md`。
2. **代码调研**：用 codegraph 找 `ToolRegistry::invoke`、
   `AgentLoop::run`、`PermissionDialog` 的现状，列出 TODO。
3. **持久化层**：`src/core/PermissionStore.{h,cpp}`，JSON 文件读写，单元测试。
4. **拦截器**：`src/core/ToolInterceptor.{h,cpp}`，实现
   `intercept(queue) -> vector<ToolResultBlock>` 串行调度。
5. **依赖中断**：`AgentLoop::run` 调用 `ToolInterceptor::intercept` 替代内
   部循环；任一失败 → 后续标记 `skipped`。
6. **TUI 接入**：把 `ReplView` 中 `info` 文本块替换为 `PermissionDialog`；
   弹窗出现时隐藏 input 框（`permissionPending_` 时 `input` 组件不渲染）。
7. **参数格式化**：`PermissionRequest::arguments` 不再传裸字符串，而是
   `json` —— `PermissionDialog::Render` 根据工具名决定怎么显示：
   - `Bash` 类：取 `command` 字段原样。
   - `WriteFile`/`EditFile`/`DeleteFile` 类：渲染 `path` + 截断 `content`。
8. **构建+测试**：cmake build、ctest、cpp-review。回归测试不能挂。

---

## 交付物清单

- [ ] `docs/prompts/permission_interceptor.md`（本文件）
- [ ] `src/core/PermissionStore.{h,cpp}` + 单元测试
- [ ] `src/core/ToolInterceptor.{h,cpp}` + 单元测试
- [ ] `src/core/AgentLoop.cpp` 改造为调用拦截器
- [ ] `src/ui/PermissionDialog.{h,cpp}` 参数格式化 + Modal 焦点
- [ ] `src/ui/ReplView.cpp` 接入 PermissionDialog，弹窗期间隐藏 input
- [ ] 全部测试通过（`ctest` 111/111 不退化）

---

## 验收标准

- 工具显示为函数名（如 `Bash`），不是 `snake.html`。
- 长参数（HTML 源码 1000 字）显示为截断键值，不撑爆 UI。
- 弹窗期间 input 框隐藏、键盘事件归弹窗。
- 用户 `No` → 当前工具 + 后续所有工具都 `is_error=true`，LLM 收到
  `permission denied` / `skipped: previous tool was denied`。
- `Yes, and never ask again` → 下次同 `tool` + 同 `pattern` 静默放行。
- 所有现有 111 条测试不回归。
