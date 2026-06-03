# AICoder

基于 LLM 的终端编码助手（C++20 + FTXUI），支持 Tool Use、多模式 Agent、会话管理，可在命令行完成文件操作、代码搜索、Shell 命令执行等开发任务。

## 特性

- **TUI 交互** — 基于 FTXUI 的终端界面，Markdown 渲染、代码语法高亮、流式增量输出
- **三种 Agent 模式** — 普通（ReAct）、反思（Reflection + Critic）、计划执行（Plan & Execute）
- **11 种内置工具** — 文件读写、搜索、Shell 执行，细粒度权限控制
- **会话管理** — `/sessions` 实时查看、切换、删除（x 键 + y/n 确认）历史会话
- **安全退出** — 双击 `Ctrl+C` 退出（首次警告、2 秒内再次确认），防止误触
- **配置导入** — `~/.aicoder/settings.json` 集中管理 API 密钥与模型参数
- **权限持久化** — AllowForever 规则写入本地，后续命中自动放行
- **流式 SSE** — libcurl + OpenAI/DeepSeek 兼容 API，支持 reasoning_content 与 cancel 中断
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
│              AgentLoop (核心循环)                          │
│  run() / runWithReflection() / runPlanExecute()          │
│  ToolInterceptor (权限管线)   ContextCompaction (压缩)    │
│  CancelToken (中断)          SelfCheck (每步自检)        │
└───┬──────────────┬────────────────┬──────────────────────┘
    │              │                │
    ▼              ▼                ▼
┌─────────┐  ┌───────────┐  ┌──────────────┐
│LlmClient│  │ToolRegistry│  │SessionStore  │
│Provider │  │11内置+Skill│  │(JSON本地存储)│
│Transport│  └───────────┘  └──────────────┘
└─────────┘
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
| `/sessions` | 查看、切换、删除历史会话 |
| `/reload-skills` | 重新发现并加载 Skill |
| `/<name> [args]` | 调用自定义命令模板或 Skill |

### 会话管理 (`/sessions`)

- `↑↓` 浏览，`Enter` 切换到选中会话
- `x` 删除会话（需 `y` / `n` 确认），当前会话不可删除
- `Esc` 返回
- 切换前自动保存当前会话，切换后加载历史消息并回放到界面

### 模式切换

`Tab` 键循环切换三种模式：

| 模式 | 方法 | 适用场景 |
|------|------|----------|
| **普通** | `run()` — ReAct 工具调用循环 | 简单任务 |
| **反思** | `runWithReflection()` — Critic 评论 + 修订（≤3 轮）+ 每步自检 | 复杂推理 |
| **计划** | `runPlanExecute()` — 规划 → 逐步执行 → 综合答案 | 多步骤任务 |

### 退出

双击 `Ctrl+C`：首次在状态栏显示红色警告，2 秒内再次按下确认退出。若 Input 框中有选中文本，`Ctrl+C` 优先执行复制。

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

### 权限模型

- 受限工具执行前弹窗询问：允许 / 永久允许 / 拒绝
- `AllowForever` 规则持久化到 `~/.config/aicoder/permissions.json`
- 后续相同模式命中自动跳过弹窗
- 任一工具被拒绝或执行失败 → 中断队列，后续工具全部跳过

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
├── sessions/                # 会话持久化 (SessionStore, Session, CliArgs)
├── rules/                   # 规则发现 → system prompt 拼接
├── ui/                      # App, ReplView, ResumePicker, Welcome,
│                            #   PermissionDialog, ConsoleRepl
├── util/                    # ThreadPool, LruCache
└── workspace/               # 文件读取、frontmatter 解析、globalDir
tests/                       # GoogleTest (137 个)
third_party/                 # FTXUI (vendor), nlohmann/json
```

## 路线图

### 已完成

- [x] 三种 Agent 模式（ReAct / Reflection / Plan & Execute）
- [x] 会话管理（查看、切换、删除）
- [x] 配置文件导入（settings.json）
- [x] 权限持久化（AllowForever）
- [x] 安全退出（双击 Ctrl+C）
- [x] Markdown 渲染 + 代码语法高亮
- [x] 流式增量合并重绘
- [x] 上下文压缩（防超限）
- [x] Cancel 中断机制
- [x] Skill 系统
- [x] 命令模板 + Tab 补全

### 短期

- [ ] 会话语义搜索
- [ ] Markdown 表格渲染增强
- [ ] 多会话并行（Tab 切换）

### 中期

- [ ] 插件系统（外部动态库，Sandbox 隔离）
- [ ] 云端配置加密同步
- [ ] 内置代码库索引（RAG）

### 长期

- [ ] 协作模式（多人实时编码）
- [ ] Agent 市场
- [ ] 可视化 Agent 决策过程
- [ ] 跨平台桌面版
