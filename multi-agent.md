# Claude Code Multi-Agent 实现详解

## 1. 核心思想

Claude Code 的 multi-agent 不是独立的 agent 进程，而是**通过 `AgentTool` 在主 agent 的循环中递归调用**——子 agent 是**独立的 QueryEngine 实例**，拥有**完全隔离的上下文**，但**复用主 agent 的工具集**（通过过滤）。

```
┌────────────────────────────────────────────────────────────┐
│  主 Agent 循环（QueryEngine A）                                │
│  ─ 完整工具集（File/Bash/Edit/...）                            │
│  ─ 模型决定："我需要分析 codebase，让 subagent 去"            │
│  ─ 调 AgentTool.call()                                        │
└──────────────────────┬─────────────────────────────────────┘
                       │
                       │ AgentTool 触发递归 query() 调用
                       ▼
┌────────────────────────────────────────────────────────────┐
│  子 Agent 循环（QueryEngine B）                                │
│  ─ 隔离上下文（新 messages[]）                                │
│  ─ 过滤后的工具集（可能只读、无 Edit）                         │
│  ─ 独立 system prompt（基于 agentType）                       │
│  ─ 独立模型（agentType 可指定）                               │
└──────────────────────┬─────────────────────────────────────┘
                       │
                       │ 子 agent 循环结束
                       ▼
            tool_result 返回给主 agent
```

---

## 2. 核心文件

| 文件 | 职责 |
|------|------|
| `packages/builtin-tools/src/tools/AgentTool/AgentTool.tsx` | AgentTool 入口（1657 行） |
| `packages/builtin-tools/src/tools/AgentTool/runAgent.ts` | 启动子 agent 的实际逻辑（959 行） |
| `packages/builtin-tools/src/tools/AgentTool/agentToolUtils.ts` | Agent 工具结果处理 |
| `packages/builtin-tools/src/tools/AgentTool/loadAgentsDir.ts` | 加载 built-in / user / project / plugin agents |
| `packages/builtin-tools/src/tools/AgentTool/forkSubagent.ts` | Fork 子 agent（递归 worker） |
| `packages/builtin-tools/src/tools/AgentTool/builtInAgents.ts` | 内置 agent 列表 |
| `packages/builtin-tools/src/tools/AgentTool/constants.ts` | AgentTool 名称常量 |
| `src/utils/agentContext.ts` | SubagentContext（上下文追踪） |
| `src/utils/teammate.ts` | Teammate 概念（多 agent 协作） |
| `src/utils/forkedAgent.ts` | Fork 模式上下文管理 |
| `src/tasks/LocalAgentTask/LocalAgentTask.tsx` | 本地后台 agent 任务管理 |
| `src/tasks/RemoteAgentTask/RemoteAgentTask.tsx` | 远程 agent 任务管理 |
| `packages/builtin-tools/src/tools/shared/spawnMultiAgent.ts` | Teammate 启动逻辑 |

---

## 3. AgentTool — 多 Agent 的入口

### 3.1 AgentTool 本质

`AgentTool` 本身是一个**普通工具**（`buildTool()`），由 `packages/builtin-tools/src/tools/AgentTool/AgentTool.tsx:284` 定义：

```typescript
export const AgentTool = buildTool({
  async prompt({ agents, tools, ... }) {
    // 返回给主 agent 的"使用说明"：列出可用的 agent 类型
    return await getPrompt(filteredAgents, ...)
  },
  name: AGENT_TOOL_NAME,
  searchHint: 'delegate work to a subagent',
  async call({ prompt, subagent_type, ... }, toolUseContext, canUseTool, ...) {
    // 1. 解析 subagent_type 找到 AgentDefinition
    // 2. 过滤工具集（按 AgentDefinition 配置）
    // 3. 调用 runAgent() 启动子 QueryEngine
    // 4. 返回 tool_result 给主 agent
  }
})
```

**关键点**：主 agent 把 `AgentTool` 当作**普通工具调用**，但 `AgentTool.call()` 内部触发**完整的子 agent 循环**。

### 3.2 Input Schema

```typescript
const baseInputSchema = z.object({
  description: z.string(),         // 3-5 词任务描述
  prompt: z.string(),             // 给子 agent 的任务
  subagent_type: z.string().optional(),  // 子 agent 类型（如 "Explore"）
  model: z.enum(['sonnet', 'opus', 'haiku']).optional(),
  run_in_background: z.boolean().optional(),
})

// 多 agent 模式扩展
const multiAgentInputSchema = z.object({
  name: z.string().optional(),    // 给 spawned agent 取名（用于 SendMessage）
  team_name: z.string().optional(),
  mode: permissionModeSchema().optional(),
  isolation: z.enum(['worktree', 'remote']).optional(),
  cwd: z.string().optional(),      // 子 agent 的工作目录
})
```

### 3.3 三种 Output

```typescript
// 同步结果
type SyncOutput = {
  status: 'completed',
  prompt: string,
  // ... 子 agent 的输出
}

// 异步结果（run_in_background: true）
type AsyncOutput = {
  status: 'async_launched',
  agentId: string,
  outputFile: string,
}

// Teammate spawn（多 agent 协作）
type TeammateSpawnedOutput = {
  status: 'teammate_spawned',
  teammate_id: string,
  tmux_session_name: string,
  // ...
}
```

---

## 4. AgentDefinition — 子 agent 配置

子 agent 通过 `AgentDefinition` 描述（`loadAgentsDir.ts:162`）：

```typescript
export type BaseAgentDefinition = {
  agentType: string           // "Explore", "Plan", "general-purpose"
  whenToUse: string           // 描述（用于 prompt 中列给主 agent）
  tools?: string[]            // 白名单（只允许这些工具）
  disallowedTools?: string[]  // 黑名单
  skills?: string[]           // 预加载的 skill
  mcpServers?: AgentMcpServerSpec[]
  hooks?: HooksSettings       // 子 agent 启动时注册的 session hooks
  color?: AgentColorName
  model?: string              // 默认模型
  effort?: EffortValue        // 推理努力度
  permissionMode?: PermissionMode
  maxTurns?: number           // 最大 turn 数（防止失控）
  background?: boolean        // 强制后台运行
  initialPrompt?: string      // 预置的初始 prompt
  memory?: AgentMemoryScope   // 持久化记忆范围
  isolation?: 'worktree' | 'remote'  // 工作目录隔离
  omitClaudeMd?: boolean      // 省略 CLAUDE.md 上下文（节省 token）
}

type AgentDefinition = BuiltInAgentDefinition | CustomAgentDefinition | PluginAgentDefinition
```

### 4.1 Agent 来源（三层优先级）

`getActiveAgentsFromList` 用 Map 实现**覆盖式合并**，后者覆盖前者：

```
built-in agents     ← 代码内置（Explore, Plan, general-purpose）
plugin agents       ← plugin 提供
user agents         ← ~/.claude/agents/*.md
project agents      ← 项目 .claude/agents/*.md
flag agents         ← --agents CLI 参数
managed agents      ← 系统管理策略
```

**优先级**：项目 > 用户 > plugin > built-in（按 Map.set 顺序）。

### 4.2 Agent 文件格式

`./claude/agents/<agent-name>.md`（Markdown + YAML frontmatter）：

```markdown
---
name: Explore
description: Read-only codebase explorer
tools: Read, Glob, Grep
disallowedTools: Edit, Write, Bash
model: sonnet
maxTurns: 30
---

You are a read-only agent. Use Read/Glob/Grep to explore code,
then return a structured summary to the parent.
```

加载后 `getSystemPrompt` 通过闭包返回 markdown body。

---

## 5. runAgent — 启动子 Agent 循环

`runAgent.ts` 是子 agent 实际启动逻辑（959 行）。核心流程：

```typescript
// 1. 准备子 agent 上下文
const subagentContext = createSubagentContext({...})

// 2. 过滤工具集（按 AgentDefinition 的 tools/disallowedTools）
const filteredTools = filterToolsForAgent(agentDef, allTools)

// 3. 构造子 agent 的 system prompt
const systemPrompt = buildEffectiveSystemPrompt({
  agentDefinition: agentDef,
  appendSystemPrompt: ...,
})

// 4. 准备隔离的 messages[]（独立 QueryEngine）
const messages = isFork ? buildForkedMessages(parentMessages) : [initialUserMessage]

// 5. 调用 query() — 启动子 QueryEngine
const generator = query(messages, {
  systemPrompt,
  tools: filteredTools,
  model: agentDef.model,
  maxTurns: agentDef.maxTurns,
  canUseTool: 子 agent 自己的权限函数,
  ...
})

// 6. 等待子 agent 循环结束
for await (const msg of generator) {
  // 收集消息、触发进度回调
}

// 7. 返回结果给主 agent
return { data: { status: 'completed', output } }
```

### 5.1 关键设计：复用主 agent 的 QueryEngine 模式

子 agent **不**创建独立的 Bun 进程，而是**在主进程中调用 `query()`**（与主 agent 同一函数）。这意味着：
- 子 agent 的循环**与主 agent 同语言同运行时**
- 可以共享一些进程级状态（但通过 `SubagentContext` 隔离）
- 子 agent 完成时**返回结果，主 agent 继续**

### 5.2 工具过滤

```typescript
// src/utils/agentToolFilter.ts
export function filterParentToolsForFork(parentTools: Tool[], agent: AgentDefinition): Tool[] {
  if (agent.tools) {
    // 白名单模式：只保留 tools 中列出的
    return parentTools.filter(t => agent.tools!.includes(t.name))
  }
  if (agent.disallowedTools) {
    // 黑名单模式：移除 disallowedTools
    return parentTools.filter(t => !agent.disallowedTools!.includes(t.name))
  }
  return parentTools
}
```

典型用法：
- `Explore` agent：`disallowedTools: [Edit, Write, Bash]` → 只读
- `Plan` agent：`disallowedTools: [Edit, Write]` → 不修改代码
- `general-purpose`：全部工具

### 5.3 上下文隔离

```typescript
// src/utils/forkedAgent.ts
export function createSubagentContext(...): SubagentContext {
  return {
    parentMessages: 父 agent 的 messages 引用（只读）
    parentToolUseContext: 父的 toolUseContext
    agentId: createAgentId(),
    isSubagent: true,
  }
}
```

通过 `SubagentContext` + `runWithAgentContext()` 隔离：
- 子 agent 的 messages 数组**独立**
- 子 agent 的文件 cache 独立（`cloneFileStateCache`）
- 子 agent 的 hooks 独立（`registerFrontmatterHooks` / `clearSessionHooks`）

---

## 6. 三种 Spawn 模式

### 6.1 同步 Spawn（默认）

```
主 agent: AgentTool.call(...)
        → runAgent(...)
        → query() (子 QueryEngine)
        → 子 agent 循环
        → 返回 tool_result
        → 主 agent 继续
```

**主 agent 阻塞直到子 agent 完成**。

### 6.2 异步 Spawn（`run_in_background: true`）

```
主 agent: AgentTool.call({ run_in_background: true })
        → 注册 LocalAgentTask（后台运行）
        → 立即返回 async_launched
        → 主 agent 继续
        → ...
        → 子 agent 完成时
        → 通过 message queue 通知主 agent
```

**机制**（`LocalAgentTask.tsx`）：
- `registerAsyncAgent()` 注册任务
- `createProgressTracker()` 跟踪进度
- `updateAgentProgress()` 实时更新
- `completeAgentTask()` / `failAgentTask()` 结束任务

主 agent 通过 SendMessage 或事件流接收子 agent 的结果。

### 6.3 Teammate Spawn（多 agent 协作）

```typescript
if (teamName && name) {
  const result = await spawnTeammate({
    name,           // 命名（如 "alice"）
    prompt,
    team_name,
    use_splitpane: true,   // tmux 分割面板
    plan_mode_required: spawnMode === 'plan',
    model,
    agent_type,
  }, toolUseContext)
  // 返回 status: 'teammate_spawned'
}
```

**机制**（`packages/builtin-tools/src/tools/shared/spawnMultiAgent.ts`）：
- 启动一个**独立的 tmux session/pane**
- 那个 pane 跑一个**独立的 Claude Code 进程**
- 子 agent 通过 `SendMessage({ to: name })` 跨进程通信
- Lead agent 通过 mailbox 接收消息

**这是真正"分布式"的 multi-agent**——不是进程内递归，而是**多 OS 进程协作**。

---

## 7. Fork Subagent（特殊模式）

`FORK_AGENT`（`forkSubagent.ts`）是另一种模式——**子 agent 继承父 agent 的部分上下文**：

```typescript
// fork 模式
const effectiveType = subagent_type ?? (isForkSubagentEnabled() ? undefined : GENERAL_PURPOSE_AGENT.agentType)
const isForkPath = effectiveType === undefined
```

**Fork vs 一般子 agent**：

| 维度 | 一般 subagent | Fork subagent |
|------|--------------|---------------|
| 上下文 | 完全独立 | 继承父 messages |
| 工具 | 过滤后 | 继承父 |
| 用途 | 隔离任务 | 在父上下文中继续 |
| `isInForkChild` | false | true |

**递归保护**：fork 子不能再 fork（避免无限递归）：
```typescript
if (toolUseContext.options.querySource === `agent:builtin:${FORK_AGENT.agentType}` ||
    isInForkChild(toolUseContext.messages)) {
  throw new Error('Fork is not available inside a forked worker.')
}
```

---

## 8. 关键文件生命周期

### 8.1 LocalAgentTask 状态机

```typescript
type LocalAgentTaskState = {
  agentId: AgentId
  description: string
  prompt: string
  status: 'pending' | 'running' | 'completed' | 'failed' | 'killed'
  progress: AgentProgress  // toolUseCount, tokenCount, lastActivity
  outputFile: string       // 输出文件路径（用于 Read 检查进度）
  startedAt: number
  completedAt?: number
}
```

状态转换通过 `updateTaskState` + `updateAgentProgress` 驱动。

### 8.2 RemoteAgentTask 状态机

远程模式（CCR / Claude Code Remote）类似但任务运行在远程环境：
- `teleportToRemote()` 把任务序列化上传
- 远程环境运行 agent
- 主 agent 通过 `getRemoteTaskSessionUrl()` 检查进度

---

## 9. 通信协议

### 9.1 主 ↔ 子（同步/异步）

- **结果**：`tool_result` 块返回给主 agent
- **进度**：`onProgress` 回调实时推送给 UI
- **完成通知**：`enqueueAgentNotification()` 把消息塞入主 agent 的消息队列

### 9.2 Teammate ↔ Teammate（多 agent 协作）

- **SendMessage** 工具（通过 `to: 'name'`）发送消息
- **Mailbox** 模式：消息进入收件箱，由收件人 agent 后续处理
- **sessionUrl**：远程模式下的会话链接

### 9.3 Subagent 上下文追踪

```typescript
// src/utils/agentContext.ts
runWithAgentContext(subagentContext, () => {
  // 这个范围内执行的所有代码都"知道"当前是子 agent
  // 用于日志、analytics、权限追踪
})
```

---

## 10. 性能与隔离权衡

| 决策 | 原因 |
|------|------|
| 进程内递归 vs 新进程 | 共享工具实现，减少 IPC 开销；多 Teammate 走 tmux/远程 |
| 工具过滤 | Explore 节省 token、避免误改；Plan 防止直接修改 |
| `omitClaudeMd` | Explore/Plan 不需要 commit/PR 规范，省 5-15 Gtok/周 |
| `maxTurns` | 防止子 agent 失控 |
| `maxResultSizeChars: 100_000` | 子 agent 输出过大时截断 |
| 独立 FileStateCache | 子 agent 文件读取不污染父 |
| 独立 session hooks | 父 hooks 不影响子，反之亦然 |

---

## 11. 完整调用栈

```
主 agent QueryEngine.submitMessage()
  → query() (src/query.ts)
    → API stream 接收 tool_use: AgentTool
    → 工具执行
      → AgentTool.call({ prompt, subagent_type })
        → 1. 解析 AgentDefinition
        → 2. 过滤工具集
        → 3. 准备子 system prompt
        → 4. 准备隔离 messages[]
        → 5. runAgent()
              → createSubagentContext()
              → query() (子 QueryEngine 启动)
                → 完整循环（多 turn）
                → 工具结果
                → 子 agent 决定下一步
                → ...
                → end_turn
              → 收集子 agent 输出
        → 6. 返回 tool_result
    → tool_result 塞回主 messages
    → 主 agent 看到结果，决定下一步
    → ...
```

---

## 12. Multi-agent 的典型用法

### 12.1 探索型（Explore）
主 agent 想知道某目录有什么文件，让 Explore 子 agent 去搜：
```typescript
AgentTool.call({
  subagent_type: 'Explore',
  description: 'Find auth code',
  prompt: 'Find all files related to authentication...',
})
// → 子 agent 用 Read/Glob/Grep 找到文件
// → 返回结构化列表给主 agent
```

### 12.2 计划型（Plan）
主 agent 想规划实现方案，让 Plan 子 agent 制定：
```typescript
AgentTool.call({
  subagent_type: 'Plan',
  description: 'Plan auth refactor',
  prompt: 'Plan a refactor of auth to use OAuth...',
})
// → 子 agent 读代码、不修改，返回详细计划
// → 主 agent 看到计划，让用户确认
```

### 12.3 后台任务
主 agent 启动一个长跑任务，立即返回继续：
```typescript
AgentTool.call({
  subagent_type: 'general-purpose',
  run_in_background: true,
  description: 'Run integration tests',
  prompt: 'Run npm test and report failures...',
})
// → 返回 async_launched
// → 子 agent 后台跑
// → 主 agent 继续做别的事
// → 完成时收到通知
```

### 12.4 多 agent 协作（Teammates）
Lead agent 启动多个 teammate 协做任务：
```typescript
AgentTool.call({
  name: 'frontend-engineer',
  team_name: 'web-project',
  prompt: 'Build login UI...',
})
// → 启动一个 tmux pane
// → 那个 pane 跑独立 Claude Code
// → Lead 通过 SendMessage({ to: 'frontend-engineer' }) 协作
```

---

## 13. 实现 Multi-Agent 的核心要点（如果要自己实现）

1. **定义 AgentDefinition** — 工具白/黑名单、模型、maxTurns、permissionMode
2. **构建 AgentTool** — 接受 prompt + agentType，触发子循环
3. **子循环复用** — 调同一个 `query()` 函数，但传入独立 messages、过滤 tools、独立 canUseTool
4. **上下文隔离** — 独立 FileStateCache、独立 hooks、独立 permission context
5. **进度回报** — `onProgress` 回调让父 agent 看到子 agent 在做什么
6. **结果回传** — 子 agent 结束时返回 `tool_result`（同步）或发通知（异步）
7. **保护机制** — maxTurns 限制、最大输出大小、递归 fork 保护
