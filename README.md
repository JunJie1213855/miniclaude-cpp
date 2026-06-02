# AICoder

基于 LLM 的终端编码助手，支持工具调用（Tool Use），可在命令行中完成文件操作、代码搜索、Shell 命令执行等开发任务。

## 设计理念

### 核心理念

**所见即终端** — 不同于 Web/桌面 IDE AICoder 采用 TUI（终端用户界面），直接在终端运行，无需额外窗口。一个 SSH 连接即可工作，覆盖全场景。

**本地优先** — 所有数据（Session、配置、规则）存储在本地文件系统，不依赖云端服务。API 调用仅用于 LLM 推理。

**安全可控** — 文件操作、Shell 命令执行前需用户确认，权限可按工具类型细粒度控制。

### 架构概览

```
┌─────────────────────────────────────────────────────┐
│                     UI 层                            │
│  ┌─────────┐  ┌─────────┐  ┌─────────────────────┐ │
│  │  App    │  │ReplView │  │  PermissionDialog   │ │
│  │(FTXUI)  │  │(FTXUI)  │  │     (FTXUI)          │ │
│  └────┬────┘  └────┬────┘  └──────────┬──────────┘ │
└───────┼────────────┼─────────────────┼─────────────┘
        │            │                 │
        ▼            ▼                 ▼
┌─────────────────────────────────────────────────────┐
│                   AgentLoop                          │
│  ┌─────────────────────────────────────────────┐   │
│  │  run() / runWithReflection() / runPlanExecute()  │
│  └──────────────────────┬──────────────────────┘   │
└─────────────────────────┼───────────────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ LlmClient    │  │ ToolRegistry │  │MessageStore  │
│ (流式推理)    │  │  (工具执行)   │  │  (上下文)    │
└──────┬───────┘  └──────┬───────┘  └──────────────┘
       │                 │
       ▼                 ▼
┌──────────────┐  ┌──────────────┐
│ Provider     │  │ Tool*        │
│ (请求编码)    │  │ (10种内置)   │
├──────────────┤  └──────────────┘
│ Transport    │
│ (HTTP/libcurl)│
└──────────────┘
```

### 三种 Agent 模式

| 模式 | 方法 | 适用场景 |
|------|------|----------|
| **普通** | `run()` | 简单任务，单轮工具调用 |
| **反思** | `runWithReflection()` | 复杂任务，引入评论家（Critic）自我批判后迭代 |
| **计划执行** | `runPlanExecute()` | 多步骤任务，先规划再分步执行 |

### 工具生态

**只读工具**（直接执行）：
- `read_file` — 读取文件内容
- `list_dir` — 列出目录
- `glob` — 文件模式匹配
- `grep` — 代码搜索

**写操作工具**（需用户确认）：
- `write_file` / `edit_file` — 文件修改
- `create_file` / `create_dir` — 创建文件/目录
- `delete_file` / `move_file` — 删除/移动
- `bash` — 执行 Shell 命令（30s 超时，32KB 输出上限）

### 消息模型

支持多模态内容块（TextBlock + ToolUseBlock + ToolResultBlock 混合），兼容 OpenAI Tools API 格式，可对接任意兼容 API（DeepSeek、OpenAI 等）。

---

## 编译

### 依赖

- **C++20** 编译器（gcc/clang）
- **libcurl**（HTTP 通信）
- **xmake**（构建工具）

### 方式一：xmake（推荐）

```bash
# 安装 xmake
curl -fsSL https://xmake.io/shget.text | bash

# 编译
xmake build aicoder

# 运行
./build/aicoder
```

### 方式二：CMake（备选）

```bash
mkdir build && cd build
cmake ..
cmake --build .
./aicoder
```

### 环境变量

```bash
export AICODER_API_KEY=sk-xxx           # 必需
export AICODER_BASE_URL=https://api.deepseek.com/v1  # 可选，默认 DeepSeek
export AICODER_MODEL=deepseek-v4-pro    # 可选
export AICODER_MAX_ITERATIONS=16        # 可选，最大迭代轮次
```

### 运行

```bash
# TUI 模式（默认）
./build/aicoder

# CLI 模式（无 TUI）
./build/aicoder --cli

# 恢复上次会话
./build/aicoder -c

# 交互式选择会话
./build/aicoder --resume
```

---

## 目录结构

```
src/
├── main.cpp              # CLI REPL 入口
├── main_tui.cpp          # TUI 入口
├── agent/                 # Agent 核心（待拆分）
├── commands/             # 斜杠命令路由
├── config/               # 环境变量配置
├── llm/                   # LLM 调用层（Client/Provider/Transport）
├── rules/                 # 系统提示规则加载
├── sessions/              # 会话存储（CliArgs/Session/SessionStore）
├── skills/                # Skill 工具注册
├── tools/                 # 10 种内置工具
├── ui/                    # FTXUI 界面组件
└── workspace/             # 工作区状态
```

---

## 未来展望

### 短期

- [ ] **Skill 系统完善** — 支持用户自定义 Skill，扩展工具生态
- [ ] **命令补全** — 斜杠命令 Tab 补全，降低使用门槛
- [ ] **会话搜索** — 基于语义的历史会话检索
- [ ] **Markdown 渲染增强** — 代码高亮、表格渲染优化

### 中期

- [ ] **多会话管理** — 并行多个 Agent 会话，Tab 切换
- [ ] **插件系统** — 外部动态库插件，Sandbox 隔离执行
- [ ] **云端配置同步** — 跨设备配置加密同步
- [ ] **内置代码库索引** — 支持大型项目的语义搜索（RAG）

### 长期

- [ ] **协作模式** — 多人实时协作编码
- [ ] **Agent 市场** — 分享和发现垂直领域 Agent
- [ ] **可视化调试** — 图形化展示 Agent 决策过程
- [ ] **跨平台桌面版** — 基于相同 Core 的 Electron/wxWidgets 桌面应用
