# Claude Code `/init` 命令实现详解

## 1. 概述

`/init` 是 Claude Code 的**内置 slash command**，专门用于在新项目（或已有项目）上**初始化 Claude Code 配置**。它的实现非常优雅——**`/init` 本身不写任何文件，所有工作都通过 prompt 驱动模型用工具完成**。

文件位置：`src/commands/init.ts`

---

## 2. 核心机制：Command as Prompt

`/init` 不是一个"执行文件操作的命令"，而是一个 **`type: 'prompt'` 类型的命令**——它只返回一段精心设计的 prompt，喂给模型，模型读完后用工具执行所有操作。

```typescript
const command = {
  type: 'prompt',           // prompt 类型 → 把字符串返回给模型
  name: 'init',
  progressMessage: 'analyzing your codebase',
  source: 'builtin',
  async getPromptForCommand() {
    // 标记项目 onboarding 完成
    maybeMarkProjectOnboardingComplete()
    // 把 prompt 喂给模型
    return [{ type: 'text', text: NEW_INIT_PROMPT }]
  }
} satisfies Command
```

**关键洞察**：
- `getPromptForCommand()` 返回的字符串会作为**用户消息**进入对话
- 模型读到 prompt 后，按指令调用 `Read`、`Glob`、`Write`、`AskUserQuestion` 等工具
- 所有"创建 CLAUDE.md"的工作都是模型用工具完成的
- `/init` 的"实现"在 prompt 里（200+ 行的设计文档），不在代码里

---

## 3. 两种模式：Feature Flag 控制

通过 `NEW_INIT` feature flag 切换：

```typescript
const useNew = feature('NEW_INIT') &&
  (process.env.USER_TYPE === 'ant' ||
    isEnvTruthy(process.env.CLAUDE_CODE_NEW_INIT))

return useNew ? NEW_INIT_PROMPT : OLD_INIT_PROMPT
```

| 模式 | 触发条件 | 行为 |
|------|----------|------|
| **新模式**（NEW_INIT） | `USER_TYPE=ant` 或 `CLAUDE_CODE_NEW_INIT=1` | 8 阶段完整流程 |
| **老模式**（默认） | 不满足上述条件 | 仅创建单个 CLAUDE.md |

---

## 4. 完整流程（8 阶段，新模式）

### Phase 1 — 询问要设置什么

通过 `AskUserQuestion` 询问用户：
- **设置哪些 CLAUDE.md？**
  - Project CLAUDE.md（团队共享，提交到 git）
  - Personal CLAUDE.local.md（个人偏好，gitignore）
  - 两者都要
- **是否同时设置 skills 和 hooks？**
  - Skills + hooks
  - 仅 Skills
  - 仅 Hooks
  - 都不，只 CLAUDE.md

### Phase 2 — 探索代码库

启动一个 **subagent** 调查代码库，要它读：
- Manifest 文件：`package.json`、`Cargo.toml`、`pyproject.toml`、`go.mod`、`pom.xml` 等
- README、Makefile、build configs
- CI 配置
- 现有 AI 配置：`.claude/rules/`、`.cursor/rules/`、`.cursorrules`、`.github/copilot-instructions.md`、`.windsurfrules`、`.clinerules`、`.mcp.json`

需要识别：
- 构建/测试/lint 命令（特别是非标准的）
- 语言、框架、包管理器
- 项目结构（monorepo、多 module、单一项目）
- 与语言默认不同的代码风格
- 非显而易见的陷阱、环境变量、工作流 quirk
- 现有 `.claude/skills/` 和 `.claude/rules/` 目录
- Formatter 配置（prettier、biome、ruff、black、gofmt、rustfmt，或 `npm run format`/`make fmt`）
- Git worktree 使用情况：`git worktree list`

### Phase 3 — 填补代码中看不出的信息

再次用 `AskUserQuestion` 问**代码里推断不出的信息**：

**项目级**（如果选了 project CLAUDE.md）：
- 非显而易见的命令、陷阱、branch/PR 约定、必须的环境设置、测试 quirk
- **不要** mark 任何选项为 "recommended"——这是团队实践，不是 best practice

**个人级**（如果选了 personal CLAUDE.local.md）：
- 团队角色？（"后端工程师"、"数据科学家"、"新人 onboarding"）
- 对代码库和语言的熟悉度？（让模型调整解释深度）
- 个人沙箱 URL、测试账号、API key 路径、本地配置
- 沟通偏好？（"简洁"、"总是解释 tradeoff"、"结尾别总结"）

**Worktree 特殊情况**：
- 如果是兄弟/外部 worktree（如 `../myrepo-feature/`）：个人内容写到 `~/.claude/<project-name>-instructions.md`，CLAUDE.local.md 是一行 stub
- 如果是嵌套 worktree（如 `.claude/worktrees/<name>/`）：不需要特殊处理

**Proposal 通过 `preview` 字段展示**：
```
question: "Does this proposal look right?"  // 简短
preview: 完整的 markdown 提议  // 在侧边栏渲染
```

**Artifacts 类型决策**（受 Phase 1 选择的硬约束）：
- **Hook**（最严格）— 确定性 shell 命令，模型不能跳过
- **Skill**（按需）— 用户或模型调 `/skill-name`
- **CLAUDE.md note**（最松）— 行为指南，不强制

**重要规则**：如果 Phase 1 选了 "Skills only"，任何 hook 都降级为 skill 或 note。永远不提议用户没选的 artifact 类型。

### Phase 4 — 写项目级 CLAUDE.md

写到项目根目录。**核心原则：每行都要通过 "删了 Claude 会不会犯错" 的测试**。

**应该包含**：
- 非标准的构建/测试/lint 命令（Claude 自己猜不到的）
- 与语言默认不同的代码风格
- 测试细节和 quirk
- 仓库礼仪（branch 命名、PR 约定、commit 风格）
- 必须的环境变量或设置步骤
- 非显而易见的陷阱或架构决策
- 现有 AI 工具配置中的重要部分

**应该排除**：
- 逐文件结构（模型能自己发现）
- 标准语言约定
- 通用建议（"写干净代码"、"处理错误"）
- 频繁变化的信息 → 用 `@path/to/import` 引用源文件
- 长教程 → 移到独立文件用 `@` 引用

**多 concern 项目**：建议拆成 `.claude/rules/` 下的多个文件：
- `code-style.md`、`testing.md`、`security.md` 等
- 可用 `paths` frontmatter 做路径作用域

**多模块项目（monorepo）**：建议每个子目录加 module-specific CLAUDE.md，Claude 在该目录工作时会自动加载。

**文件前缀**：
```markdown
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
```

**已存在 CLAUDE.md**：读它，提具体修改（用 diff），不静默覆盖。

### Phase 5 — 写个人级 CLAUDE.local.md

写到项目根目录，**自动加入 .gitignore**（保持私密）。

**应该包含**：
- 用户角色和代码库熟悉度（让 Claude 调整解释）
- 个人沙箱 URL、测试账号、本地配置
- 个人工作流或沟通偏好

**Worktree 特殊处理**：见 Phase 3。

**已存在**：读它，提具体新增，不静默覆盖。

### Phase 6 — 创建 Skills（如果 Phase 1 选了）

**Step 1**：从 Phase 3 的 preference queue 取出 `skill` 条目，每条创建为 SKILL.md。

**Step 2**：额外建议（如果发现）：
- 特定任务的参考知识（subsystem 的约定、模式、风格指南）
- 可重复的工作流（deploy、fix issue、release process、verify changes）

**文件位置**：`.claude/skills/<skill-name>/SKILL.md`

**模板**：
```yaml
---
name: <skill-name>
description: <what it does and when to use it>
---

<Instructions for Claude>
```

**有副作用的 skill**（如 `/deploy`、`/fix-issue 123`）加 `disable-model-invocation: true`，只允许用户触发。用 `$ARGUMENTS` 接受输入。

**已存在 skills**：不覆盖，只补全。

### Phase 7 — 额外的优化建议

**检查项**：

1. **GitHub CLI**：跑 `which gh`（Windows 用 `where gh`），没装就建议
2. **Linting**：Phase 2 没找到 lint 配置就建议
3. **Hooks**（如果 Phase 1 选了）：
   - **目标文件**：默认根据 Phase 1 选择 — project → `.claude/settings.json`（团队共享），personal → `.claude/settings.local.json`
   - **事件选择**：
     - "每次编辑后" → `PostToolUse` + matcher `Write|Edit`
     - "Claude 完成后" / "我审之前" → `Stop` 事件
     - "运行 bash 前" → `PreToolUse` + matcher `Bash`
     - **"git commit 前"** → **不是 hooks.json hook**，matcher 不能按命令内容过滤。用 git pre-commit hook（`.git/hooks/pre-commit`、husky、pre-commit framework）
   - **构造 hook 流程**（用 `update-config` skill）：
     1. dedup check
     2. 为本项目构造
     3. pipe-test raw
     4. wrap
     5. write JSON
     6. `jq -e` 验证
     7. live-proof（对可触发的 matcher）
     8. cleanup
     9. handoff

### Phase 8 — 总结 + Plugin 推荐

回顾写了什么。基于项目特征给出**相关**的 plugin 推荐（按影响力排序）：
- 前端项目：frontend-design、playwright
- 测试覆盖少：建议设置 test framework
- **必推荐**：`/plugin install skill-creator@claude-plugins-official`
- **必推荐**：用 `/plugin` 浏览官方 plugins

---

## 5. 老模式（OLD_INIT_PROMPT）

简单版本：
- 分析代码库
- 创建 CLAUDE.md
- 包含内容：
  1. 构建/lint/test 命令
  2. 高层架构
- 不询问用户
- 已有 CLAUDE.md → 提改进建议

---

## 6. 关键设计原则

| 原则 | 体现 |
|------|------|
| **极简** | CLAUDE.md 只写删了会犯错的内容 |
| **不重复** | 不写模型能自己发现的信息（文件结构、语言约定） |
| **不编造** | 不写"Common Tasks"、"Tips"等通用章节，除非有具体来源 |
| **频繁变化 → 引用** | 用 `@path/to/import` 让模型读最新版 |
| **个性化分层** | 项目级（共享）+ 个人级（gitignore） |
| **强制提问** | 用 `AskUserQuestion` 而非猜测用户意图 |
| **推荐不走极端** | `preview` 字段展示 markdown 提议，对话框不会覆盖 |

---

## 7. 文件产物清单

| 位置 | 类型 | 何时创建 |
|------|------|----------|
| `./CLAUDE.md` | 项目级（共享） | Phase 4 |
| `./CLAUDE.local.md` | 个人级（gitignore） | Phase 5 |
| `./.claude/rules/*.md` | 项目级（共享，按主题拆分） | Phase 4（多 concern 时） |
| `./.claude/skills/<name>/SKILL.md` | 按需 | Phase 6 |
| `./.claude/settings.json` | 项目级（共享） | Phase 7（hooks） |
| `./.claude/settings.local.json` | 个人级 | Phase 7（hooks） |
| `~/.claude/<project>-instructions.md` | 个人全局 | Phase 5（worktree 特殊情况） |
| `./.gitignore` 新增行 | 个人级 | Phase 5（自动加 `CLAUDE.local.md`） |

---

## 8. 与 Subagent 系统的关系

Phase 2 的代码库调查**启动一个 subagent**（不是主 agent 自己干）：
- 隔离上下文（大量文件读取不会污染主对话）
- 加快速度
- 复用 subagent 能力

---

## 9. 关键技术细节

### 9.1 `AskUserQuestion` 的 `preview` 字段

提议通过 `preview` 字段展示：
- 对话框**覆盖**之前的输出（之前文本消息被隐藏）
- `preview` 渲染 markdown 侧边栏（类似 plan mode）
- `question` 字段是**纯文本**

**重要约束**：preview 框**不可滚动**——必须紧凑：
- 每项一行
- 项之间无空行
- 无 header
- 例：
  ```
  • **Format-on-edit hook** (automatic) — `ruff format <file>` via PostToolUse
  • **/verify skill** (on-demand) — `make lint && make typecheck && make test`
  • **CLAUDE.md note** (guideline) — "run lint/typecheck/test before marking done"
  ```

### 9.2 Worktree 自动检测

`git worktree list` 检测是否多 worktree。如果是兄弟/外部 worktree，必须用 `~/.claude/<project-name>-instructions.md` + stub import 模式，**永远不要在项目 CLAUDE.md 里放个人引用**。

### 9.3 Preference Queue 机制

Phase 3 收集的所有偏好形成一个**统一的 preference queue**：
```typescript
type Entry = {
  type: 'hook' | 'skill' | 'note'
  description: string
  target: 'CLAUDE.md' | 'CLAUDE.local.md' | 'settings.json' | 'skills/'
  details: any  // Phase 2 收集的具体细节（如实际 test 命令）
}
```

Phase 4-7 各自从 queue 里消费相关条目。

### 9.4 Phase 1 选择作为"硬过滤器"

如果 Phase 1 选 "Skills only"：
- 任何提议的 hook 降级为 skill 或 note
- 不提议用户没选的 artifact 类型

这保证**最终产出**严格匹配用户**初始选择**。

---

## 10. 相关文件

| 文件 | 内容 |
|------|------|
| `src/commands/init.ts` | `/init` 命令定义（33 行代码 + 200 行 prompt） |
| `src/commands/commands.ts` | `Command` 类型定义 |
| `src/projectOnboardingState.ts` | `maybeMarkProjectOnboardingComplete()` |
| `src/utils/autonomyAuthority.ts` | `AUTONOMY_AGENTS_PATH_POSIX` 路径常量 |
| `src/utils/envUtils.ts` | `isEnvTruthy()` |
| `src/utils/claudemd.ts` | CLAUDE.md 加载发现机制 |
| `packages/builtin-tools/src/tools/SkillTool/` | Skill tool 实现 |
| `packages/builtin-tools/src/tools/AgentTool/` | Subagent tool 实现（Phase 2 用） |
| `packages/builtin-tools/src/tools/ExitPlanModeTool/` | Plan mode 组件（preview 渲染参考） |

---

## 11. 用 `/init` 引导出来的思维模型

`/init` 本身是一个**绝佳的 prompt engineering 案例**：
- 把复杂任务（初始化整个仓库）拆成 8 个阶段
- 每个阶段有明确的输入/输出
- 强制使用 `AskUserQuestion` 收集用户意图
- 通过 `preview` 字段解决对话框覆盖问题
- 用 preference queue 模式串联多阶段
- 用 Phase 1 选择作为硬过滤器

这种**"prompt 驱动的命令"**设计是 Claude Code 的一个核心模式——很多 `/xxx` 命令都遵循这个套路。
