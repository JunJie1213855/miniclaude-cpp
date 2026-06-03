# AICoder

基于 LLM 的终端编码助手（C++20 + FTXUI），支持 Tool Use、多模式 Agent、Supervisor 多 Agent 协同、会话管理，可在命令行完成文件操作、代码搜索、Shell 命令执行等开发任务。

## 核心特性

- **TUI 交互** — 基于 FTXUI 的终端界面，Markdown 渲染、表格、代码语法高亮、流式增量输出
- **三种 Agent 模式** — 普通（ReAct）、反思（Reflection + Critic）、计划执行（Plan & Execute）
- **11 种内置工具** — 文件读写、搜索、Shell 执行，细粒度权限控制
- **Supervisor 多 Agent** — 主 Agent 通过 `run_sub_agent` 工具委派任务给拥有受限工具集的专家子 Agent
- **后台子 Agent** — `run_in_background: true` 异步执行，并发 2 线程，结果通过 `get_subagent_status` / `get_subagent_result` 工具查询
- **会话管理** — `/sessions` 实时查看、切换、删除（x 键 + y/n 确认）历史会话
- **安全退出** — 双击 `Ctrl+C` 退出（首次警告、2 秒内再次确认），防止误触
- **配置导入** — `~/.aicoder/settings.json` 集中管理 API 密钥与模型参数
- **权限持久化** — AllowForever 规则写入本地，后续命中自动放行
- **Skill 系统** — 外部 YAML/Markdown 技能注册，`/<name>` 即调用
- **斜杠命令** — Tab 补全、模板展开、`$ARGUMENTS` 占位符

## 架构

```
┌──────────────────────────────────────────────────────────┐
│                       UI 层                               │
│  App  ReplView  ResumePicker  Welcome  PermissionDialog │
└──────────────────────┬───────────────────────────────────┘
                       │
┌──────────────────────┼───────────────────────────────────┐
│              AgentLoop (主循环)                          │
│  run() / runWithReflection() / runPlanExecute()          │
│  ToolInterceptor (权限管线)   ContextCompaction (压缩)    │
│  CancelToken (中断)          SelfCheck (每步自检)        │
└───┬──────────────┬────────────────┬──────────────────────┘
    │              │                │
    ▼              ▼                ▼
┌─────────┐  ┌───────────┐  ┌──────────────┐
│LlmClient│  │ToolRegistry│  │SessionStore  │
│Provider │  │11内置+Skill│  │(JSON本地存储)│
│Transport│  │+3子代理工具│  └──────────────┘
└─────────┘  └───────────┘
                       │
                       │ run_sub_agent (sync/async)
                       ▼
              ┌──────────────────┐
              │ SubAgentManager  │
              │ bg_pool (2 threads)│
              │ + SubAgentRunner │
              │ + filtered tools │
              └──────────────────┘
```

### LLM 调用栈

```
LlmClient (抽象) → DefaultLlmClient
  ├── Provider (抽象) → OpenAIProvider  (请求编码, 兼容 DeepSeek API)
  └── Transport (抽象) → HttpTransport (libcurl SSE 流式 POST, 120s 超时)
```

## 编译

### 依赖

- C++20 编译器（gcc ≥ 11 / clang ≥ 14）
- libcurl
- CMake ≥ 3.16 或 xmake

### CMake

```bash
mkdir build && cd build
cmake ..
cmake --build .
./aicoder_tui
```

### xmake

```bash
xmake build aicoder
./build/aicoder
```

## 配置

### 环境变量

```bash
export AICODER_API_KEY=sk-xxx           # 必需
export AICODER_BASE_URL=https://api.deepseek.com/v1  # 可选
export AICODER_MODEL=deepseek-v4-pro    # 可选
export AICODER_MAX_ITERATIONS=16        # 可选
```

### 配置文件

将密钥写入 `~/.aicoder/settings.json`，无需每次 export：

```json
{
  "env": {
    "AICODER_AUTH_TOKEN": "sk-xxx",
    "AICODER_BASE_URL": "https://api.deepseek.com/anthropic",
    "AICODER_DEFAULT_MODEL": "deepseek-v4-pro[1m]"
  }
}
```

**优先级**: 环境变量 > settings.json。`AICODER_API_KEY` 环境变量优先于文件中的 `AICODER_AUTH_TOKEN`。

## 运行

```bash
# TUI 模式（默认）
./aicoder_tui

# 恢复上次会话
./aicoder_tui -c

# 交互式选择会话
./aicoder_tui --resume

# 列出所有会话
./aicoder_tui --list-sessions
```

## 运行时

### 斜杠命令

| 命令 | 说明 |
|------|------|
| `/quit`, `/exit` | 退出程序 |
| `/clear` | 清空对话，开始新会话 |
| `/sessions` | 查看、切换、删除历史会话（x 键删除） |
| `/agents` | 查看可用子代理 |
| `/skills` | 查看可用技能 |
| `/reload-skills` | 重新发现并加载 Skill |
| `/<name> [args]` | 调用自定义命令模板或 Skill |

### 模式切换（Tab 键）

| 模式 | 方法 | 适用场景 |
|------|------|----------|
| **普通** | `run()` — ReAct 工具调用循环 | 简单任务 |
| **反思** | `runWithReflection()` — Critic 评论 + 修订（≤3 轮）+ 每步自检 | 复杂推理 |
| **计划** | `runPlanExecute()` — 规划 → 逐步执行 → 综合 | 多步骤任务 |

### 退出

双击 `Ctrl+C`：首次在状态栏显示红色警告，2 秒内再次按下确认退出。Input 框有选中文本时 `Ctrl+C` 优先复制。

## 多 Agent 协同

### 同步调用

主 Agent 在对话中直接请求子 Agent（LLM 自行决定何时调用）：

```
你: 用 cpp-reviewer 审查 src/core/AgentLoop.cpp
主 Agent: run_sub_agent({agent:"cpp-reviewer", task:"..."})
    → 同步阻塞，结果回到主对话
```

### 异步后台调用

主 Agent 同时发起多个子任务（LLM 用 `run_in_background: true`）：

```
你: 用三个子代理并行审查 AgentLoop.cpp、SubAgentRunner.cpp、StreamParser.cpp
主 Agent: 3 个 run_sub_agent({run_in_background: true})
    → 立即返回 task_id × 3
    → bg_pool (2 线程) 并发执行
    → 完成时 UI 通知: [后台子代理 X 完成 (subagent-N)]
```

### 三个子 Agent 工具

| 工具名 | 用途 |
|--------|------|
| `run_sub_agent` | 委派任务给子 Agent，可选 `run_in_background: true` |
| `get_subagent_status` | 查询任务状态（status / agent / elapsed_ms） |
| `get_subagent_result` | 阻塞取回结果（`wait=true` + `timeout_ms`） |

**保证**：
- 共享 `cancel_` 原子 — Ctrl+C 一键取消所有子 Agent
- 输出截断 100K 字符
- 独立 `ThreadPool` (2 线程)，不抢主 REPL

### 添加自定义子代理

在 `~/.aicoder/agents/<name>/AGENT.md` 创建：

```markdown
---
name: my-reviewer
description: Reviews pull requests for style
tools: ["read_file", "grep", "glob"]
max_iterations: 5
---
You are a code reviewer specialized in style consistency...
```

下次启动 AICoder 自动发现，下次 `/agents` 就会显示。优先级：项目级 > 用户级。

## 工具

### 只读（直接执行）

| 工具 | 说明 |
|------|------|
| `read_file` | 读取文件内容 |
| `list_dir` | 列出目录 |
| `glob` | 文件模式匹配 |
| `grep` | 代码内容搜索 |

### 需权限（用户确认）

| 工具 | 说明 |
|------|------|
| `write_file` / `edit_file` | 写入 / 编辑文件 |
| `create_file` / `create_dir` | 创建文件 / 目录 |
| `delete_file` / `move_file` | 删除 / 移动文件 |
| `bash` | Shell 命令（30s 超时，32KB 输出上限） |
| `skill` | 动态注册的 Skill 工具 |

### 多 Agent 工具

| 工具 | 说明 |
|------|------|
| `run_sub_agent` | 委派给子 Agent（同步或后台） |
| `get_subagent_status` | 查询后台任务状态 |
| `get_subagent_result` | 阻塞取回后台任务结果 |

### 权限模型

- 受限工具执行前弹窗询问：允许 / 永久允许 / 拒绝
- `AllowForever` 规则持久化到 `~/.config/aicoder/permissions.json`
- 子 Agent 工具集是父工具集经 `filter(names)` 隔离后的子集

## 目录结构

```
src/
├── main.cpp                 # TUI 入口
├── config/                  # 环境变量 + settings.json 配置
├── core/                    # AgentLoop, ToolRegistry, ToolInterceptor,
│                            #   Message, PermissionStore, Errors, Version
├── llm/                     # LlmClient → OpenAIProvider → HttpTransport
│                            #   StreamParser, Response, StreamDelta
├── tools/                   # 11 种内置工具
├── commands/                # 斜杠命令路由与模板发现
├── skills/                  # Skill 注册与 SkillTool
├── subagent/                # SubAgent 系统
│                            #   SubAgentDef, SubAgentRegistry,
│                            #   SubAgentRunner, SubAgentManager (后台池),
│                            #   SubAgentTool, SubAgentStatusTool,
│                            #   SubAgentResultTool
├── sessions/                # 会话持久化 (SessionStore, Session, CliArgs)
├── rules/                   # 规则发现 → system prompt 拼接
├── ui/                      # App, ReplView, ResumePicker, Welcome,
│                            #   PermissionDialog, ConsoleRepl, MarkdownTable
├── util/                    # ThreadPool, LruCache
└── workspace/               # 文件读取、frontmatter 解析、globalDir
tests/                       # GoogleTest (195 个)
third_party/                 # FTXUI (vendor), nlohmann/json
```

## 路线图

### 已完成

- [x] 三种 Agent 模式（ReAct / Reflection / Plan & Execute）
- [x] 会话管理（查看、切换、删除）
- [x] 配置文件导入（settings.json）
- [x] 权限持久化（AllowForever）
- [x] 安全退出（双击 Ctrl+C）
- [x] Markdown 渲染 + 表格 + 代码语法高亮
- [x] 流式增量合并重绘
- [x] 上下文压缩（防超限）
- [x] Cancel 中断机制
- [x] Skill 系统
- [x] 命令模板 + Tab 补全
- [x] Supervisor 多 Agent 同步/异步协同
- [x] 后台子 Agent + 状态/结果查询工具

### 短期

- [ ] 会话语义搜索
- [ ] 子 Agent 嵌套深度限制
- [ ] MCP Client（连接外部 MCP Server 添加工具）

### 中期

- [ ] 插件系统（外部动态库，Sandbox 隔离）
- [ ] 云端配置加密同步
- [ ] 内置代码库索引（RAG）

### 长期

- [ ] 协作模式（多人实时编码）
- [ ] Agent 市场
- [ ] 可视化 Agent 决策过程
- [ ] 跨平台桌面版
