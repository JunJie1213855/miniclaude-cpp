# AICoder TypeScript TUI 设计

## 概述

为 AICoder TypeScript 版本实现一个现代极简风格的 TUI 界面，替代现有的基础 ConsoleRepl。

## 技术选型

- **框架**：ink（React 风格 TUI）
- **语言**：TypeScript
- **Node.js**：标准库 + ink

## 视觉风格

- **背景**：深灰 `#1a1a2e` 或纯黑 `#0d0d14`
- **文字**：白色/浅灰 `#e0e0e0`
- **用户消息**：偏蓝 `#6c9bff`
- **AI 消息**：白色
- **工具调用**：深紫 `#9d7cd8`，带缩进
- **Thinking 动画**：`⠋⠙⠹⠸` 旋转字符

## 核心组件

```
App
├── Header（标题栏：AIcoder 版本 + 当前模式）
├── MessageList（消息列表，可滚动）
│   ├── UserMessage
│   ├── AssistantMessage
│   └── ToolCallMessage（展开详情，内嵌子组件）
├── ThinkingIndicator（模型思考时的动画指示器）
└── InputArea（底部固定输入框）
```

## 布局

```
┌──────────────────────────────────────────┐
│ AICoder  SP1a                      [?]   │  ← Header
├──────────────────────────────────────────┤
│  [User] 你好                              │
│  [AI] 你好！有什么可以帮助你的？           │
│  [Tool] read_file                        │  ← 展开的工具调用
│    path: README.md                       │
│    result: # Project Overview            │
│  [AI] 这是项目概述...                     │
├──────────────────────────────────────────┤
│ > 输入消息...                       [⏎]  │  ← InputArea（固定底部）
└──────────────────────────────────────────┘
```

## 交互逻辑

### 会话管理

- **新建会话**：启动显示欢迎页，输入后创建新 session
- **继续会话（-c）**：加载最近的 session 历史，恢复上下文
- **自动保存**：每轮对话后自动保存到 `~/.aicoder/sessions/<id>/session.json`
- **无侧边栏**：通过 `/sessions` 命令交互式选择会话

### 消息展示

- **用户消息**：蓝色前缀，右侧对齐或特殊标识
- **AI 消息**：白色，左侧对齐
- **工具调用**：展开显示 `[工具名]` + 参数 + 结果，可折叠
- **流式输出**：文本直接追加到消息内，reasoning 内容单独一行淡色显示

### 输入交互

- **底部固定输入框**
- **Tab**：补全命令（如 `/clear`、`/quit`、`/sessions`）
- **Ctrl+C**：中断输入，返回空提示符
- **方向键**：上下切换历史输入

## 依赖

需要新增的 npm 依赖：
- `ink` - TUI 框架
- `react` - ink 的 peer dependency

## 实现计划

1. 安装 ink 和 react 依赖
2. 创建 `ui/TuiApp.tsx` - 主应用组件
3. 创建 `ui/Header.tsx` - 标题栏组件
4. 创建 `ui/MessageList.tsx` - 消息列表组件
5. 创建 `ui/Message.tsx` - 单条消息（User/Assistant/Tool）
6. 创建 `ui/InputArea.tsx` - 底部输入框
7. 创建 `ui/Welcome.tsx` - 欢迎页
8. 更新 `main.ts` - 入口切换到 TUI 模式
9. 保留 ConsoleRepl 作为 fallback（无 ink 环境时）

## 文件结构

```
ui/
├── index.ts              - 导出
├── TuiApp.tsx            - 主应用（App 组件）
├── Header.tsx             - 标题栏
├── MessageList.tsx        - 消息列表
├── Message.tsx            - 单条消息
├── ToolCall.tsx           - 工具调用展开组件
├── InputArea.tsx          - 底部输入框
├── ThinkingIndicator.tsx  - Thinking 动画
└── Welcome.tsx            - 欢迎页
```