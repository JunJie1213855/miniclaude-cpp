# AICoder SP1b 实现计划 — FTXUI TUI 外壳

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 SP1a（控制台 agentic 内核）之上套 FTXUI TUI 外壳。`AgentLoop` 内核零改动。新增 `ReplView` 组件（消息列表 + 输入框），worker 线程跑 `AgentLoop::run`，新消息通过 `ScreenInteractive::Post()` 刷新到 UI 线程，「思考中」输入框置灰禁止并发提交。

**Architecture:** `App`（`ScreenInteractive` + 事件循环）持有 `ReplView`（FTXUI 组件）。`ReplView` 持有 `AgentLoop` + `CommandRouter` + UI 状态。worker 线程通过 `Post()` 跨线程安全地写入 UI 状态，`AgentLoop` 本身不知道线程存在。

**Tech Stack:** C++20 · CMake 3.16（FetchContent FTXUI）· FTXUI 6.x（CMake 自动下载）· GoogleTest · 现有 `aicoder_core` 静态库（零改动）。

**Spec:** `docs/superpowers/specs/2026-05-28-aicoder-sp1-design.md` §SP1b。

**工作目录:** `~/lib/CppCode/AICoder/`（SP1a 已就绪，42 tests 通过）。

---

## 文件结构

| 文件 | 职责 | 状态 |
|---|---|---|
| `CMakeLists.txt` | 加 FetchContent FTXUI + `aicoder_tui` 可执行（link aicoder_core + FTXUI） | 修改 |
| `src/ui/ReplView.h` | FTXUI 组件：消息列表 + 输入框 + 命令拦截 | 新增 |
| `src/ui/ReplView.cpp` | 组件定义、事件处理、worker 线程启动 | 新增 |
| `src/ui/App.h` | `App` 类：`makeApp()` 返回 `Component`，持有 `ScreenInteractive` 生命周期 | 新增 |
| `src/ui/App.cpp` | screen 创建、Run() 事件循环 | 新增 |
| `src/main.cpp` | 保留 SP1a 装配（Config + tools + ConsoleRepl）；加命令行参数切换 TUI 模式（`--tui`） | 修改 |

---

## Task 1: CMakeLists.txt 加 FTXUI

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 读取当前 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(AICoder CXX)
# ... 当前内容（保留所有现有配置）...
```

- [ ] **Step 2: 在 `find_package(GTest REQUIRED)` 前插入 FetchContent FTXUI**

在 `find_package(Threads REQUIRED)` 后、`find_package(CURL REQUIRED)` 前添加：

```cmake
# --- FTXUI -----------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
  GIT_TAG v6.1.9
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(ftxui)
```

- [ ] **Step 3: 在 `add_executable(aicoder "${PROJECT_SOURCE_DIR}/src/main.cpp")` 后添加 TUI 可执行**

```cmake
# SP1b: TUI 可执行（依赖 aicoder_core + FTXUI）
add_executable(aicoder_tui "${PROJECT_SOURCE_DIR}/src/main_tui.cpp")
target_link_libraries(aicoder_tui PRIVATE aicoder_core ftxui::ftxui)
```

**注意**：`main_tui.cpp`（TUI 入口）和 `main.cpp`（控制台入口）分开两个文件，避免 SP1a 控制台 build 意外依赖 FTXUI。

- [ ] **Step 4: 运行 CMake 配置**

Run: `cmake -S . -B build 2>&1 | grep -E "ftxui|ERROR"`
Expected: 打印 FTXUI FetchContent 下载进度，最后 `Configuring done`。无 ERROR。下载 FTXUI 源码可能需要 1-2 分钟（首次）。如遇网络超时，重试一次。

- [ ] **Step 5: 运行构建**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: 构建成功（`aicoder` 和 `aicoder_tui` 两个 target）。`aicoder_tui` 链接 ftxui 可能产生一些警告，但无链接错误。

- [ ] **Step 6: 提交**

```bash
git add CMakeLists.txt
git commit -m "chore: add FTXUI via FetchContent + aicoder_tui target"
```

---

## Task 2: App（ScreenInteractive 生命周期）

**Files:**
- Create: `src/ui/App.h`, `src/ui/App.cpp`

- [ ] **Step 1: 写 `src/ui/App.h`**

```cpp
#pragma once
#include <memory>
#include "ftxui/component/component.hpp"

namespace aicoder {
class App {
public:
  // 创建 App，返回顶层 Component（主循环持有 screen 的生命周期）
  static ftxui::Component make(App& owner);
  App();
  ~App();
  // 启动 TUI 事件循环（在 main 里调用）。退出时返回。
  void run();

private:
  friend class App;
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}
```

- [ ] **Step 2: 写 `src/ui/App.cpp`**

```cpp
#include "App.h"
#include "ftxui/screen/screen.hpp"
#include "ftxui/dom/elements.hpp"
#include <iostream>

namespace aicoder {

class App::Impl {
public:
  ftxui::Component component;
  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Interactive();

  Impl(ftxui::Component comp) : component(comp) {}
};

// static factory（接收 owner 引用，Component 捕获 owner 的 shared_ptr 存活）
ftxui::Component App::make(App& owner) {
  // 子类/持有者可在 App::Impl 里放子组件
  // 这里先返回空 Container，后续 ReplView 替换
  return ftxui::Container({});
}

App::App() : impl_(std::make_unique<Impl>(ftxui::Container({}))) {}
App::~App() = default;

void App::run() {
  screen.Loop(impl_->component);
}

}
```

- [ ] **Step 3: 写占位 `src/main_tui.cpp`**

```cpp
#include <iostream>
#include "config/Config.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "core/AgentLoop.h"
#include "commands/CommandRouter.h"
#include "llm/DefaultLlmClient.h"
#include "llm/OpenAIProvider.h"
#include "llm/HttpTransport.h"
#include "tools/ReadFileTool.h"
#include "tools/ListDirTool.h"
#include "ui/App.h"

using namespace aicoder;

int main() {
  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }
  ToolRegistry registry;
  registry.registerTool(makeReadFileTool());
  registry.registerTool(makeListDirTool());
  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  // TODO: wire ReplView + App in next task
  std::cout << "App placeholder (TUI wiring in Task 3)\n";
  return 0;
}
```

- [ ] **Step 4: 构建验证**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: `aicoder_tui` 链接成功（可能有 FTXUI 警告但无错误）。`./build/aicoder_tui` 运行打印 "App placeholder" 并正常退出。

- [ ] **Step 5: 提交**

```bash
git add src/ui/App.h src/ui/App.cpp src/main_tui.cpp
git commit -m "chore: App skeleton with ScreenInteractive lifecycle"
```

---

## Task 3: ReplView（消息列表 + 输入框 + worker 线程）

**Files:**
- Create: `src/ui/ReplView.h`, `src/ui/ReplView.cpp`
- Test: `tests/ReplViewTest.cpp`（人工验收，无自动化测试——见 Task 5）

- [ ] **Step 1: 写 `src/ui/ReplView.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include <functional>
#include "ftxui/component/component.hpp"

namespace aicoder {

struct UIMessage {
  std::string text;
  bool is_user = false;
  bool is_error = false;
};

class ReplView {
public:
  // onSubmit(text): 用户按 Enter 后的回调，由 App/父组件注入
  using SubmitCallback = std::function<void(const std::string& text)>;

  // 构造：持有 AgentLoop/CommandRouter/系统提示，绑定 submit 回调
  ReplView(SubmitCallback onSubmit);

  // 返回 FTXUI Component（供 App 嵌入）
  ftxui::Component component();

  // worker 线程调用此方法把新消息推入 UI 线程（线程安全，通过 Post 投递）
  void appendMessage(UIMessage msg);

  // 发送错误消息（工具失败 / LLM 错误）
  void appendError(const std::string& msg);

  // 设置思考中状态（worker 用）
  void setThinking(bool v);

  // 获取当前消息列表（渲染用）
  std::vector<UIMessage> messages() const;

private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}
```

- [ ] **Step 2: 写 `src/ui/ReplView.cpp`（核心实现）**

```cpp
#include "ReplView.h"
#include "ftxui/screen/screen.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include <chrono>

namespace aicoder {

struct UIMessage { std::string text; bool is_user = false; bool is_error = false; };

class ReplView::Impl {
public:
  ftxui::Component component;
  ftxui::ScreenInteractive* screen = nullptr;  // 由 App 注入
  std::vector<UIMessage> messages_;
  std::string input_text_;
  bool thinking_ = false;
  std::weak_ptr<Impl> self_;  // 弱引用，避免循环

  Impl() {
    auto input = ftxui::Input(&input_text_, "输入消息，按 Enter 发送...");
    input->on_enter = [this] { onEnter(); };

    auto message_renderer = ftxui::Renderer([this] {
      std::vector<ftxui::Element> els;
      for (const auto& m : messages_) {
        std::string prefix = m.is_user ? "❯ " : (m.is_error ? "✗ " : "◆ ");
        auto style = m.is_error ? ftxui::dim : ftxui::text;
        els.push_back(ftxui::text(prefix + m.text) | style);
      }
      return ftxui::vbox(std::move(els)) | ftxui::flex;
    });

    auto input_renderer = ftxui::Renderer(input, [this, input] {
      auto content = ftxui::text(input_text_) | ftxui::flex;
      if (thinking_)
        return ftxui::hbox({content, ftxui::text(" [思考中...] ") | ftxui::dim});
      return content;
    });

    component = ftxui::Container({
      message_renderer | ftxui::flex | ftxui::yframe | ftxui::vscroll_indicator,
      input_renderer
    });
    component = ftxui::CatchEvent(component, [this](const ftxui::Event& e) {
      return false;  // 暂不做全局快捷键
    });
  }

  void onEnter() {
    if (thinking_) return;  // 思考中禁止重复提交
    std::string txt = input_text_;
    if (txt.empty()) return;
    input_text_.clear();

    auto self = self_.lock();
    if (self && onSubmit_) {
      thinking_ = true;
      refresh();
      onSubmit_(txt);
    }
  }

  void refresh() {
    if (screen) screen->Post([self = self](){});
  }

  SubmitCallback onSubmit_;
  std::weak_ptr<Impl> self_weak() { return self_; }
};

ReplView::ReplView(SubmitCallback onSubmit) : impl_(std::make_shared<Impl>()) {
  impl_->self_ = impl_;
  impl_->onSubmit_ = std::move(onSubmit);
}

ftxui::Component ReplView::component() { return impl_->component; }

void ReplView::appendMessage(UIMessage msg) {
  auto self = impl_->self_weak().lock();
  if (!self || !impl_->screen) return;
  impl_->screen->Post([self, msg = std::move(msg), this]() {
    impl_->messages_.push_back(std::move(msg));
    impl_->thinking_ = false;
  });
}

void ReplView::appendError(const std::string& msg) {
  appendMessage({msg, false, true});
}

void ReplView::setThinking(bool v) {
  impl_->thinking_ = v;
}

std::vector<UIMessage> ReplView::messages() const { return impl_->messages_; }

}
```

> **说明**：`Impl` 有 `std::weak_ptr<Impl> self_` 用于 `Post` 回调中持有自己（避免每次 `Post` 捕获 `shared_ptr` 造成引用计数增加导致消息不释放）。`screen` 指针由 `App` 在组装时注入。

- [ ] **Step 3: 修改 `App.h` 和 `App.cpp`（注入 screen 到 ReplView）**

`src/ui/App.h` 在 `App` 类内加：
```cpp
void setScreen(ftxui::ScreenInteractive* s);   // ReplView 需要 screen->Post()
```

`src/ui/App.cpp`：
```cpp
#include "ReplView.h"
#include "core/AgentLoop.h"
#include "commands/CommandRouter.h"
#include "llm/DefaultLlmClient.h"
#include "config/Config.h"
#include "core/Message.h"
#include "core/Errors.h"
#include "tools/ReadFileTool.h"
#include "tools/ListDirTool.h"

class App::Impl {
public:
  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Interactive();
  ReplView replView;
  AgentLoop& agentLoop;
  CommandRouter router;
  std::vector<Message> messages;

  Impl(AgentLoop& loop, CommandRouter& router)
      : agentLoop(loop), router(router),
        replView([this](const std::string& input) { onSubmit(input); }) {
    replView.setScreen(&screen);
  }

  void onSubmit(const std::string& input) {
    // 命令拦截
    if (router.handle(input, messages) == CommandResult::Quit) {
      screen.Exit();
      return;
    }
    if (router.handle(input, messages) == CommandResult::Cleared) {
      replView.appendMessage({"[对话已清空]", false, false});
      return;
    }
    messages.push_back(userText(input));
    replView.setThinking(true);
    replView.appendMessage({input, true, false});

    auto loopPtr = &agentLoop;
    auto msgsPtr = &messages;
    auto rv = &replView;
    auto scr = &screen;
    std::thread([=]() {
      try {
        std::string reply = loopPtr->run(*msgsPtr);
        scr->Post([=]() {
          rv->appendMessage({reply, false, false});
          rv->setThinking(false);
        });
      } catch (const LlmError& e) {
        scr->Post([=]() {
          rv->appendError(std::string("[错误] ") + e.what());
          rv->setThinking(false);
        });
      }
    }).detach();
  }
};

void App::setScreen(ftxui::ScreenInteractive* s) {
  impl_->replView.setScreen(s);
}

// make() 返回 ReplView 的 Component，外面套 Border
ftxui::Component App::make(App& owner) {
  return ftxui::Border(impl_->replView.component());
}
```

- [ ] **Step 4: 更新 `src/main_tui.cpp`（完整装配）**

```cpp
#include <iostream>
#include "config/Config.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "core/AgentLoop.h"
#include "commands/CommandRouter.h"
#include "llm/DefaultLlmClient.h"
#include "llm/OpenAIProvider.h"
#include "llm/HttpTransport.h"
#include "tools/ReadFileTool.h"
#include "tools/ListDirTool.h"
#include "ui/App.h"

using namespace aicoder;

int main() {
  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }

  ToolRegistry registry;
  registry.registerTool(makeReadFileTool());
  registry.registerTool(makeListDirTool());

  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  AgentLoop loop(client, registry, config.max_iterations);
  CommandRouter router;

  App app(loop, router, "你是 AICoder，一个命令行编码助手。");
  app.run();
  return 0;
}
```

> **注意**：`App` 需要新构造函数：`App(AgentLoop&, CommandRouter&, const std::string& systemPrompt)`。把 `Impl` 里的装配移出来到 App 构造函数。

- [ ] **Step 5: 编译验证**

Run: `cmake --build build -j 2>&1 | grep -E "error:|ftxui" | head -10`
Expected: 无编译错误（有 FTXUI 警告可忽略）。`aicoder_tui` 链接成功。

- [ ] **Step 6: 提交**

```bash
git add src/ui/ReplView.h src/ui/ReplView.cpp src/ui/App.h src/ui/App.cpp src/main_tui.cpp
git commit -m "feat: ReplView TUI component with worker thread + Post() refresh"
```

---

## Task 4: main.cpp 改为模式切换器

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 替换 `src/main.cpp`（SP1a 控制台入口 → 模式切换）**

```cpp
#include <iostream>
#include "config/Config.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "core/AgentLoop.h"
#include "commands/CommandRouter.h"
#include "llm/DefaultLlmClient.h"
#include "llm/OpenAIProvider.h"
#include "llm/HttpTransport.h"
#include "tools/ReadFileTool.h"
#include "tools/ListDirTool.h"
#include "ui/ConsoleRepl.h"

using namespace aicoder;

// SP1a 默认：控制台 REPL
int runConsole(Config& config) {
  ToolRegistry registry;
  registry.registerTool(makeReadFileTool());
  registry.registerTool(makeListDirTool());
  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  AgentLoop loop(client, registry, config.max_iterations);
  CommandRouter router;
  std::string systemPrompt =
      "你是 AICoder，一个运行在命令行的编码助手。"
      "你可以调用 read_file 读取文件、list_dir 列出目录。"
      "需要文件或目录信息时请主动调用工具，不要编造内容。";
  ConsoleRepl repl(loop, router, systemPrompt);
  repl.run();
  return 0;
}

int main(int argc, char* argv[]) {
  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }
  return runConsole(config);
}
```

- [ ] **Step 2: 编译验证**

Run: `cmake --build build -j 2>&1 | tail -2`
Expected: 两个 target 全部构建成功。

- [ ] **Step 3: 验证控制台入口仍然正常**

Run: `env -u AICODER_API_KEY ./build/aicoder; echo "exit=$?"`
Expected: `[配置错误] 请设置环境变量 AICODER_API_KEY`，`exit=1`。

- [ ] **Step 4: 提交**

```bash
git add src/main.cpp
git commit -m "refactor: main.cpp back to SP1a console-only, aicoder_tui is separate entry"
```

---

## Task 5: 人工验收（无自动化测试）

- [ ] **Step 1: 构建 + 准备 key**

```bash
cmake --build build -j
export AICODER_API_KEY=<你的 key>
```

- [ ] **Step 2: 启动 TUI（无 key 报错）**

```bash
./build/aicoder_tui
```
Expected: `aicoder_tui` 需要 `AICODER_API_KEY`（用现有配置即可）。如有报错同 `aicoder`。

- [ ] **Step 3: 多轮对话（无工具）**

```bash
./build/aicoder_tui
> 你好，介绍一下你自己
> 你能做什么
```
Expected: 消息列表实时滚动，回复边生成边显示（「思考中...」提示消失后）。

- [ ] **Step 4: ReadFile 工具闭环**

```bash
./build/aicoder_tui
> 读一下 ./CMakeLists.txt 讲了什么
```
Expected: 思考中 → 工具执行 → 真实内容 → 模型基于内容回答，消息列表中看到工具结果。

- [ ] **Step 5: 并发保护**

在「思考中...」状态输入文字并按 Enter。
Expected: 输入被忽略（输入框置灰/思考中提示），不重复提交。

- [ ] **Step 6: 错误展示**

输入一个不存在的文件（如 `./不存在.txt`）。
Expected: 显示 `✗ [工具执行失败: ...]` 或 `✗ [错误] HTTP 500`，消息列表中出现红色错误行，思考状态退出，输入框恢复。

- [ ] **Step 7: 命令**

```bash
./build/aicoder_tui
> /clear
> /quit
```
Expected: `/clear` 显示 `[对话已清空]`，`/quit` 干净退出。

- [ ] **Step 8: 与 SP1a 行为一致性（证明内核零改动）**

用相同输入（读文件、工具失败）对比 `./build/aicoder` 与 `./build/aicoder_tui` 的模型回答是否一致。

- [ ] **Step 9: 记录验收**

```bash
git commit --allow-empty -m "test: SP1b TUI acceptance passed (manual)"
```

---

## Self-Review

**Spec coverage:**
- FTXUI FetchContent + aicoder_tui target → Task 1 ✓
- App: ScreenInteractive 生命周期 → Task 2 ✓
- ReplView: 消息列表 + 输入框 + 命令拦截 → Task 3 ✓
- worker 线程跑 AgentLoop::run → Task 3 (Impl::onSubmit) ✓
- `screen.Post()` 跨线程刷新 → Task 3 ✓
- 思考中输入框置灰 + 并发保护 → Task 3 ✓
- main.cpp 模式切换 → Task 4 ✓
- 人工验收（无工具 / 工具闭环 / 并发保护 / 错误 / 命令 / 内核一致性）→ Task 5 ✓
- `AgentLoop` 内核零改动 → 整个 SP1b 期间未改任何 `src/core/`、`src/llm/`、`src/tools/`、`src/config/`、`src/commands/` ✓

**类型一致性：** `ReplView::Impl::screen` 是 `ftxui::ScreenInteractive*`（由 App 持有生命周期），`setScreen()` 在 App 组装时注入。`std::weak_ptr<Impl>` 用于 Post 捕获避免循环引用。`UIMessage{text, is_user, is_error}` 与 ConsoleRepl 的打印逻辑一致（prefix "❯" / "◆" / "✗"）。

**C++20 note:** FTXUI 源码包含协程支持，CMake 配置正常即可，不需要额外 flags。