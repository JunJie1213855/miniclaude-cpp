# AICoder

基于 LLM 的终端编码助手（C++20 + FTXUI），支持 Tool Use、多模式 Agent、会话管理，可在命令行完成文件操作、代码搜索、Shell 命令执行等开发任务。

## 核心特性

- **TUI 交互** — 基于 FTXUI 的终端界面，支持 Markdown 渲染、代码高亮、流式输出
- **三种 Agent 模式** — 普通（ReAct）、反思（Reflection）、计划执行（Plan & Execute）
- **11 种内置工具** — 文件读写、搜索、Shell 执行，细粒度权限控制
- **会话管理** — `/sessions` 命令实时查看、切换、删除历史会话
- **Skill 系统** — 外部 YAML/Markdown 技能注册，`/<skill>` 即调用
- **斜杠命令** — Tab 补全、模板展开、`$ARGUMENTS` 占位符
- **配置文件导入** — `~/.aicoder/settings.json` 集中管理 API 密钥和模型配置
- **流式 SSE** — libcurl + OpenAI/DeepSeek 兼容 API，支持 reasoning_content

## 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                       UI 层                               │
│  App (FTXUI)  ReplView  SessionPicker  PermissionDialog │
└──────────────────────┬───────────────────────────────────┘
                       │
┌──────────────────────┼───────────────────────────────────┐
│              AgentLoop (核心循环)                          │
│  run() / runWithReflection() / runPlanExecute()          │
│  ToolInterceptor (权限管线)  ContextCompaction (压缩)     │
│  CancelToken (中断)                                       │
└───┬──────────────┬────────────────┬──────────────────────┘
    │              │                │
    ▼              ▼                ▼
┌─────────┐  ┌───────────┐  ┌──────────────┐
│LlmClient│  │ToolRegistry│  │SessionStore  │
│Provider │  │ 11内置+Skill│  │(JSON本地存储)│
│Transport│  └───────────┘  └──────────────┘
└─────────┘
```

### LLM 调用栈

```
LlmClient (抽象) → DefaultLlmClient
  ├── Provider (抽象) → OpenAIProvider  (请求编码, 兼容 DeepSeek)
  └── Transport (抽象) → HttpTransport (libcurl, SSE 流式 POST)
```

## 编译

### 依赖

- C++20 编译器（gcc ≥ 11 / clang ≥ 14）
- libcurl（HTTP 通信）
- xmake 或 CMake ≥ 3.16

### xmake（推荐）

```bash
xmake build aicoder
./build/aicoder
```

### CMake

```bash
mkdir build && cd build
cmake ..
cmake --build .
./aicoder_tui
```

## 配置

### 环境变量

```bash
export AICODER_API_KEY=sk-xxx           # 必需
export AICODER_BASE_URL=https://api.deepseek.com/v1  # 可选
export AICODER_MODEL=deepseek-v4-pro    # 可选
export AICODER_MAX_ITERATIONS=16        # 可选
```

### 配置文件（推荐）

将密钥和模型写入 `~/.aicoder/settings.json`，无需每次 export：

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
./build/aicoder_tui

# 恢复上次会话
./build/aicoder_tui -c

# 交互式选择会话
./build/aicoder_tui --resume

# 列出所有会话
./build/aicoder_tui --list-sessions
```

## 运行时命令

| 命令 | 说明 |
|------|------|
| `/quit`, `/exit` | 退出程序 |
| `/clear` | 清空当前对话，开始新会话 |
| `/sessions` | 查看、切换、删除历史会话 |
| `/reload-skills` | 重新发现并加载 Skill |
| `/<name>` | 调用自定义命令或 Skill |

### 会话管理（`/sessions`）

- `↑↓` 浏览会话列表，`Enter` 切换到选中会话
- `x` 删除选中会话（需 `y/n` 确认，当前会话不可删除）
- `Esc` 返回
- 切换前自动保存当前会话，切换后立即加载历史消息

### 模式切换

`Tab` 键循环切换三种对话模式：

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **普通** | ReAct 工具调用循环 | 简单任务 |
| **反思** | Critic 评论 + 修订（最多 3 轮） | 复杂推理 |
| **计划** | 先规划步骤 → 逐步执行 → 综合 | 多步骤任务 |

## 工具生态

### 只读工具（直接执行）

| 工具 | 说明 |
|------|------|
| `read_file` | 读取文件内容 |
| `list_dir` | 列出目录 |
| `glob` | 文件模式匹配 |
| `grep` | 代码内容搜索 |

### 写操作工具（需用户确认）

| 工具 | 说明 |
|------|------|
| `write_file` / `edit_file` | 写入/编辑文件 |
| `create_file` / `create_dir` | 创建文件/目录 |
| `delete_file` / `move_file` | 删除/移动文件 |
| `bash` | Shell 命令（30s 超时，32KB 上限） |

### 权限控制

- 首次使用受限工具时弹窗询问：允许 / 永久允许 / 拒绝
- `AllowForever` 规则持久化到 `~/.config/aicoder/permissions.json`
- 后续命中相同模式自动放行

## 目录结构

```
src/
├── main.cpp                 # TUI 入口
├── config/                  # 环境变量 + settings.json 配置
├── core/                    # AgentLoop, ToolRegistry, ToolInterceptor,
│                            #   Message, PermissionStore, Errors
├── llm/                     # LlmClient → OpenAIProvider → HttpTransport
│                            #   StreamParser, Response, StreamDelta
├── tools/                   # 11 种内置工具
├── commands/                # 斜杠命令路由与模板发现
├── skills/                  # Skill 注册与 SkillTool
├── sessions/                # 会话持久化 (SessionStore)
├── rules/                   # 规则发现 → system prompt
├── ui/                      # App, ReplView, SessionPicker, ResumePicker,
│                            #   Welcome, PermissionDialog, ConsoleRepl
├── util/                    # ThreadPool, LruCache
└── workspace/               # 文件读取、frontmatter 解析、globalDir
tests/                       # GoogleTest 单元测试 (137 个)
third_party/                 # FTXUI (vendor), nlohmann/json
```

## 路线图

### 短期

- [x] 三种 Agent 模式（ReAct / Reflection / Plan & Execute）
- [x] 会话管理（查看、切换、删除）
- [x] 配置文件导入（settings.json）
- [x] 权限持久化（AllowForever）
- [ ] 会话语义搜索
- [ ] Markdown 表格渲染增强

### 中期

- [ ] 多会话并行（Tab 切换）
- [ ] 插件系统（外部动态库，Sandbox 隔离）
- [ ] 云端配置加密同步
- [ ] 内置代码库索引（RAG）

### 长期

- [ ] 协作模式（多人实时编码）
- [ ] Agent 市场
- [ ] 可视化 Agent 决策过程
- [ ] 跨平台桌面版（Electron / wxWidgets）
