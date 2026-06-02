# AICoder SP1 设计 — Agentic 内核（OpenAI 兼容）

> 日期：2026-05-28
> 范围：SP1（最小 agentic 内核），分两个内部里程碑 SP1a（控制台）+ SP1b（TUI）。
> 性质：学习项目，优先清晰度与循序渐进，而非生产级健壮性 / 最大复用。
> 上游计划：见 `task.md`（本 spec 是其 SP1 的定稿设计）。

---

## 0. 核心认知

整个 Claude Code 本质是一个循环：调模型 → 若返回工具调用则执行并把结果回灌 → 重入循环 →
直到模型不再要工具（end_turn）才停下等用户。所有高级功能都是这个循环上的叠加层。SP1 的目标
就是把这个**迭代 agentic 循环 + 原生结构化工具调用**亲手跑通——这是 80% 的认知收获。

---

## 1. 已确定的决策

| 决策点 | 结论 |
|---|---|
| 与现有 CppAIService / HTTPServer 关系 | 完全从零，不复用现有代码（纯学习） |
| LLM 后端 | **OpenAI 兼容（DeepSeek/Qwen）**，原生 `tool_calls` 结构化工具调用 |
| 后端可迁移性 | 内部用 canonical content-block Message 模型 + `Provider` 适配器隔离；换 Claude = 加一个 `AnthropicProvider`，内核与工具层零改动 |
| SP1 结构 | **控制台优先，分两步**：SP1a 单线程控制台内核 → SP1b 套 FTXUI + worker 线程 |
| SP1 工具 | 两个只读工具：`ReadFile` + `ListDir`（验证「多工具注册 + 模型自主选工具」） |
| 控制台命令 | `/quit` `/clear`（循环前本地拦截，Stage 5 雏形） |
| 路径安全 | SP1 **不做**路径限定（只读，留给 SP2 权限层） |
| 暂不纳入 SP1 | 流式输出、权限层、skills、commands(模板类)、rules、agents、multi-agent |

---

## 2. 架构与隔离原则

**核心隔离线**：`AgentLoop`（内核）只依赖 `LlmClient` 抽象接口，不知道自己背后是控制台还是
TUI、是 OpenAI 还是 Claude。控制台（SP1a）与 TUI（SP1b）都只是驱动同一个内核的不同 I/O 外壳。

### 2.1 模块布局

```
AICoder/
├── CMakeLists.txt                # C++20；find_package(CURL/GTest)；nlohmann 走 find_path + vendored fallback（沿用 HTTPServer 约定）
├── third_party/nlohmann/json.hpp # vendored 兜底单头
├── src/
│   ├── main.cpp                  # 读 Config → 装配 → SP1a: ConsoleRepl.run() / SP1b: ReplView.run()
│   ├── core/
│   │   ├── Message.h             # Role + ContentBlock(variant) + Message —— canonical 模型
│   │   ├── Tool.h                # Tool 接口
│   │   ├── ToolRegistry.h/.cpp   # register / invoke(name,args) / schemas()
│   │   └── AgentLoop.h/.cpp      # ⭐ 迭代循环；依赖 LlmClient 接口 + ToolRegistry
│   ├── llm/
│   │   ├── LlmClient.h           # 抽象接口：Response send(msgs, tools)  ← AgentLoop 只认它
│   │   ├── Provider.h            # 抽象：encode(msgs,tools)->json / decode(json)->Response
│   │   ├── OpenAIProvider.h/.cpp # canonical ⇄ OpenAI JSON（tool_calls 解析）
│   │   ├── HttpTransport.h/.cpp  # 纯 libcurl POST
│   │   └── DefaultLlmClient.h/.cpp # Provider + Transport 组装成 LlmClient
│   ├── tools/
│   │   ├── ReadFileTool.cpp      # 读文件内容
│   │   └── ListDirTool.cpp       # 列目录
│   ├── config/Config.h/.cpp      # env 读 key/base_url/model，fail-fast
│   ├── commands/CommandRouter.h/.cpp # /quit /clear 本地拦截
│   └── ui/
│       ├── ConsoleRepl.h/.cpp    # SP1a：cin/cout，单线程直驱 AgentLoop
│       └── ReplView.h/.cpp       # SP1b：FTXUI + worker 线程 + ScreenInteractive::Post()
└── tests/
    ├── ToolRegistryTest.cpp
    ├── OpenAIProviderTest.cpp
    └── AgentLoopTest.cpp         # fake LlmClient
```

### 2.2 关键数据结构

**Canonical Message（忠于 Claude Code 的 content blocks）：**

```cpp
enum class Role { System, User, Assistant, Tool };
struct TextBlock       { std::string text; };
struct ToolUseBlock    { std::string id, name; json input; };
struct ToolResultBlock { std::string tool_use_id, content; bool is_error = false; };
using  ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock>;
struct Message { Role role; std::vector<ContentBlock> content; };
```

`OpenAIProvider` 负责互转：
- `ToolUseBlock` ⇄ assistant 消息的 `tool_calls: [{id, function:{name, arguments}}]`
- `ToolResultBlock` ⇄ `{role:"tool", tool_call_id, content}`
- 解码：OpenAI `tool_calls` → `ToolUseBlock`(s)；`content` → `TextBlock`

**Tool 接口：**

```cpp
struct Tool {
  std::string name, description;
  json input_schema;                                      // 手写 JSON Schema
  std::function<std::string(const json& input)> execute;  // 成功返回结果文本；失败抛 ToolError
};
```

工具失败用异常表达，`ToolRegistry::invoke` 捕获后由 `AgentLoop` 包成 `is_error=true` 的
`ToolResultBlock` 回灌模型。

### 2.3 LLM 三层职责

| 层 | 职责 | 不负责 |
|---|---|---|
| `HttpTransport` | 纯 libcurl POST：发字节、收字节、处理 HTTP 状态码 | 不懂 LLM / JSON 语义 |
| `OpenAIProvider` | canonical Message ⇄ OpenAI JSON 编解码（含 tool_calls） | 不发网络 |
| `DefaultLlmClient` | 编排：`messages+tools → provider.encode → transport.post → provider.decode` | — |

`AgentLoop` 依赖抽象 `LlmClient`，测试时注入 fake。

**`Response`（`LlmClient.send` 的返回值）：**

```cpp
struct Response {
  Message assistant_message;   // role=Assistant，content 含 TextBlock 和/或 ToolUseBlock
  std::string finish_reason;   // "stop" / "tool_calls" / ...（来自 OpenAI，Provider 归一化）
};
```

`AgentLoop` 判断「是否有工具要执行」只看 `assistant_message` 里有没有 `ToolUseBlock`；
`finish_reason` 仅作辅助/日志。基建失败时 `send` 抛 `LlmError`，不返回 `Response`。

---

## 3. 数据流与控制流

### 3.1 一轮用户输入（SP1a 控制台，单线程）

```
用户输入一行
  → CommandRouter 先看是不是 /quit /clear      ← 循环前拦截器
        /quit  → 退出
        /clear → 清空 messages（保留 system），不调 API
        其它   → 当成 user message
  → messages.push_back(user msg)
  → AgentLoop::run(messages, registry):
        for (iter = 0; iter < maxIterations; ++iter):
            Response resp = llm.send(messages, registry.schemas())   // 非流式，一次 POST
            messages.push_back(resp.assistant_message)               // text 和/或 tool_use
            if (resp 无 tool_use)            // finish_reason=="stop"
                break                        // end_turn，回去等用户
            for (每个 ToolUseBlock tu):
                result = registry.invoke(tu.name, tu.input)          // 可能抛 ToolError
                toolMsg.content.push_back(ToolResultBlock{tu.id, result, is_error})
            messages.push_back(toolMsg)
            continue                         // 自动重入，不等用户  ⭐ agentic 灵魂
        if (iter == maxIterations) → 追加系统提示「达到迭代上限」，停下
  → 打印最终 assistant 文本
```

### 3.2 终止条件
- **正常结束**：OpenAI 响应 `finish_reason == "stop"` 且无 `tool_calls` → break，等用户。
- **安全阀**：`maxIterations`（默认 16，可配）。撞上限优雅停下并告知用户，不崩溃。

### 3.3 错误流（两类，区别对待）

| 类别 | 例子 | 处理 |
|---|---|---|
| **工具失败** | 文件不存在、目录读不了、参数非法 | `ToolError` → 包成 `ToolResultBlock{is_error:true}` **回灌模型**，让它自己纠错/换路径重试。**循环不中断。** |
| **基建失败** | HTTP 非 2xx、网络断、响应 JSON 解析失败、缺 API key | 抛 `LlmError` → **中断本轮循环**，把人类可读错误显示给用户，保留 messages，等人类介入。**不喂给模型。** |

本质区别：工具失败是模型能在对话内恢复的事；基建失败是需要人类介入的事。

### 3.4 线程边界

- **SP1a**：`cin` 阻塞读 → 同线程跑 `AgentLoop`（含阻塞 HTTP）→ `cout` 打印。零并发，零锁。
- **SP1b**：
  - UI 线程：只渲染 + 收键盘。
  - 回车 → 推入 user message → spawn **worker 线程** 跑 `AgentLoop::run`。
  - worker 每产生新消息 → `screen.Post([=]{ /* 改 UI 状态 */ })` 回 UI 线程刷新。
  - 跨线程**只走 `Post`**，不直接碰 UI 状态。`AgentLoop` 不知道线程存在。
  - worker 运行期间输入框置灰/显示「思考中」，禁止并发提交。

关键不变量：**`AgentLoop` 在 SP1a 与 SP1b 是同一份代码、同样调用**，区别只在谁在哪个线程调用、结果怎么回显。

---

## 4. 配置与密钥

```cpp
struct Config {
  std::string api_key;    // AICODER_API_KEY    必填，缺则启动即报错退出
  std::string base_url;   // AICODER_BASE_URL   默认 https://api.deepseek.com/v1
  std::string model;      // AICODER_MODEL      默认 deepseek-chat
  int max_iterations = 16;
};
Config Config::fromEnv();  // 缺 api_key → 打印「请设置 AICODER_API_KEY」并退出
```

绝不硬编码、不写进仓库。`main.cpp` 第一步即 `Config::fromEnv()`，失败立刻退出，不进 TUI/循环。

---

## 5. 工具

| 工具 | input schema | 行为 | 失败 |
|---|---|---|---|
| `ReadFile` | `{ "path": string }` | 返回文件内容 | 找不到/读失败 → `ToolError` 回灌 |
| `ListDir` | `{ "path": string }` | 返回目录条目（一行一个，标注文件/目录） | 不存在/非目录 → `ToolError` 回灌 |

两者一起注册进 `ToolRegistry`，`registry.schemas()` 把两个 schema 交给 `LlmClient.send`，
让 SP1 就能验证「多工具注册 + 模型自主选工具」。SP1 不做路径限定（只读；留给 SP2 权限层）。

---

## 6. 控制台命令

`CommandRouter` 在消息进循环**之前**拦截，纯本地、不调 API：
- `/quit` → 结束程序
- `/clear` → 清空 `messages`（保留 system prompt）
- 非命令文本 → 原样作为 user message

SP1b 复用同一 `CommandRouter`（输入框里打 `/clear` 一样生效）。

---

## 7. 测试（GoogleTest，沿用 HTTPServer 的 tests/ 约定）

- **`ToolRegistryTest`**：注册成功 / 调用已注册工具 / 调用未知工具报错 / ToolError 被捕获包装。
- **`OpenAIProviderTest`**：
  - 编码：canonical（含 tool_use、tool 结果消息）→ OpenAI JSON。
  - 解码：OpenAI 纯文本响应 → canonical；带 tool_calls 响应 → canonical。
  - 编解码往返一致。
- **`AgentLoopTest`**（fake `LlmClient`，不碰网络）：
  1. 纯文本一轮即停。
  2. 单 tool_use → 执行 → 回灌 → 第二轮停。
  3. 连续多轮工具不卡死。
  4. 撞 `maxIterations` 优雅停下。
  5. 工具抛错 → `is_error` 回灌、循环继续。
- TUI 层（ConsoleRepl/ReplView）难单测 → **人工验收**。

---

## 8. 分段验收标准

**SP1a（控制台内核）完成 =**
- [ ] 终端跑起来，`cin/cout` 多轮对话正常
- [ ] 问「读一下 xxx 文件讲了什么」→ 模型发 ReadFile tool_use → 执行回灌 → 模型基于内容作答
- [ ] 问「列一下这个目录有啥」→ 走 ListDir，同样闭环
- [ ] 连续多工具轮不卡死，撞上限有保护
- [ ] 文件不存在时模型收到 error 并能换路径/解释（验证工具失败回灌）
- [ ] `/clear` `/quit` 生效；缺 `AICODER_API_KEY` 时启动即清晰报错
- [ ] GoogleTest 全绿

**SP1b（TUI 层）完成 =**
- [ ] FTXUI 消息列表 + 输入框，回车提交
- [ ] HTTP/工具在 worker 线程跑，UI 不卡；新消息经 `Post` 实时刷新
- [ ] 「思考中」输入框置灰，禁止并发提交
- [ ] 与 SP1a 完全相同的对话/工具/命令行为（证明内核未改）

---

## 9. roadmap 备注（影响 task.md，非 SP1 实现项）

SP2 顺序建议：**流式 → 权限层 + 第一个副作用工具（Bash/Write）同时落地 → commands → rules → skills**。
权限层必须跟副作用工具一起进，不能被拖后，否则 agent 会在没有闸门时执行写/删操作。

---

## 10. C++ 特有难点（SP1 范围）

| 难点 | 应对 |
|---|---|
| TUI 事件循环 vs 阻塞 IO（SP1b） | worker 线程 + `ScreenInteractive::Post()` |
| 工具 JSON Schema | nlohmann/json 手写构造 |
| `std::variant` content block 的访问 | `std::visit` / `std::get_if` |
| 消息历史所有权 | 值语义 `std::vector<Message>`，按需 move |
