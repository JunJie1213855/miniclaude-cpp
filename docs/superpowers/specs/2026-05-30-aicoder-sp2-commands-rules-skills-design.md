# AICoder SP2 — Commands / Rules / Skills 设计

## Context

AICoder 是从零用 C++ 实现的 TUI 版 Claude Code（学习项目）。SP1 已跑通核心循环 + 原生
tool_calls + 多个工具，本会话又叠加了流式、权限、ReAct/Reflection/Plan-Execute 模式、命令补全。

本设计实现 task.md 路线图的 Stage 5–7 三个「循环上的叠加层」：

- **Commands（斜杠命令）**：在消息进循环**之前**拦截。
- **Rules（上下文）**：启动时把 `CLAUDE.md` / `rules/` 收集进 **system prompt**。
- **Skills（按需指令包）**：用一个 `skill` 工具，调用时把对应 markdown 正文读进消息列表。

三者本质相同 —— 都是「往消息列表 / system prompt 里塞东西」。本设计把它们做成**文件驱动**
（贴近真 Claude Code），并从**全局 `~/.aicoder/` + 项目 `./`（含 `./.aicoder/`）**两处发现，
**项目覆盖全局**（Rules 为追加合并）。

## 已定决策

| 决策点 | 结论 |
|---|---|
| 定义来源 | **全部从文件加载**（不在 C++ 里硬编码命令/规则/技能） |
| 发现范围 | **全局 `~/.aicoder/` + 项目 `./` 与 `./.aicoder/`**；项目覆盖全局 |
| 命令参数 | `$ARGUMENTS` = 命令名之后的整行文本（不做 `$1..$N`） |
| 技能调用 | 模型调用 `skill` 工具（普通 Tool），工具返回 SKILL.md 正文回灌循环 |
| 技能告知 | 技能清单（name: description）注入 system prompt；正文按需由工具加载 |
| 构建顺序 | 文件发现工具 → Rules → Commands → Skills |

## 模块布局（新增）

```
src/
├── workspace/Workspace.{h,cpp}     # 0. 文件发现（全局/项目目录、列 *.md、读文件）
├── rules/Rules.{h,cpp}             # 1. loadRules() + buildSystemPrompt()
├── commands/CommandRouter.{h,cpp}  # 2. 扩展：内置命令 + 模板命令注册/展开
├── commands/CommandRegistry.{h,cpp}#    模板命令发现/存储
└── skills/SkillRegistry.{h,cpp}    # 3. 技能发现/存储
    skills/SkillTool.{h,cpp}        #    makeSkillTool(registry)
```

## 0. 文件发现工具（共用基础件）

`src/workspace/Workspace.{h,cpp}`，纯自由函数，供三者复用：

- `std::filesystem::path globalDir()` → `$HOME/.aicoder`（无 HOME 时返回空，调用方跳过）。
- `std::filesystem::path projectDir()` → `当前工作目录/.aicoder`。
- `std::optional<std::string> readFile(const path&)` → 文件存在且可读时返回内容。
- `std::vector<path> listMarkdown(const path& dir)` → 递归收集 `dir` 下所有 `*.md`，按相对
  路径**排序**（保证确定顺序）；目录不存在返回空。

错误：任何不存在/不可读 → 安全跳过（返回空 optional / 空 vector），不抛。

## 1. Rules

`src/rules/Rules.{h,cpp}`：

- `std::string loadRules()` 按顺序拼接（全局在前、项目在后），各段以空行分隔：
  1. `~/.aicoder/CLAUDE.md`
  2. `~/.aicoder/rules/**/*.md`（`listMarkdown` 排序）
  3. `./CLAUDE.md`
  4. `./.aicoder/rules/**/*.md`
- `std::string buildSystemPrompt(const std::string& base, const std::string& skillList)`
  → `base`，非空时依次追加 `loadRules()` 段与 `## 可用技能\n<skillList>` 段（**任一为空则省略该段**，
  避免出现空标题）。`skillList` 见第 3 节。
- **顺带去重**：`main.cpp` / `main_tui.cpp` 里重复的硬编码 systemPrompt 抽成共享常量
  `kBaseSystemPrompt`（放 `rules/Rules.h` 或新建 `core/Prompt.h`），两个入口都用 `buildSystemPrompt`。
- 语义：Rules 是**追加**（全部拼进上下文），非按名覆盖。

## 2. Commands

扩展 `src/commands/`：

- **内置本地命令**（保留在 `CommandRouter`，不调 API）：`/quit` `/exit` → Quit；`/clear` → Cleared。
- **模板命令**（新，文件驱动）：`CommandRegistry` 从 `~/.aicoder/commands/*.md` +
  `./.aicoder/commands/*.md` 发现；文件名 `review.md` → 命令 `/review`；**项目同名覆盖全局**。
  文件可选 frontmatter `description:`（用于补全菜单），其余正文为 prompt 模板。
- **接口改造**：`CommandResult` 增加 `Prompt` 变体；`handle` 返回小结构：
  ```cpp
  struct CommandOutcome { CommandResult result; std::string prompt; };
  CommandOutcome handle(const std::string& input, std::vector<Message>& messages) const;
  ```
  - `/quit`/`/exit` → `{Quit, ""}`；`/clear` → `{Cleared, ""}`（仍清 messages，保留 System）。
  - `/<name> args` 命中模板命令 → 展开 `$ARGUMENTS`（命令名后整行）→ `{Prompt, 展开文本}`。
  - 其它 → `{NotACommand, ""}`。
- **调用方**（`App::onSubmit` / `ConsoleRepl`）：`Prompt` 时把 `userText(outcome.prompt)` 入队
  再跑循环（而非原样 input）；`NotACommand` 仍按原 input 入队。
- **补全菜单数据化**：`CommandRouter::availableCommands()` → `vector<{name, description}>`
  （内置 + 发现的模板命令）。`ReplView` 不再硬编码 `kSlashCommands`，改由 `App` 把该列表传入
  （构造时或 setter）。前缀过滤/↑↓/Tab/Enter 逻辑不变。

## 3. Skills

`src/skills/`：

- `SkillRegistry` 从 `~/.aicoder/skills/<name>/SKILL.md` + `./.aicoder/skills/<name>/SKILL.md`
  发现；解析 frontmatter（`name` / `description`，缺失则用目录名 / 空）+ 正文（指令）。
  **项目同名覆盖全局**。提供 `list()`（返回格式化清单字符串，每行 `- name: description`，无技能则空串）
  和 `body(name)`（返回 SKILL.md 正文）。
- `makeSkillTool(const SkillRegistry&)`：一个普通 `Tool`，name=`skill`，input
  `{ "name": string }`（input_schema 的 name 用 enum = 已发现技能名兜底）；`execute` 读
  `registry.body(name)` 返回正文（找不到 → 抛 `ToolError`）。注册进 `ToolRegistry`（在
  `registerBuiltinTools` 之外单独注册，因为它依赖运行时发现的 registry）。
- **告知模型**：`SkillRegistry::list()` 生成「name: description」清单，经 `buildSystemPrompt`
  注入 system prompt 的「## 可用技能」段；正文按需由 `skill` 工具加载（"工具产出即指令"）。

## 数据流

```
启动：
  Rules     = loadRules()
  commands  = CommandRegistry(发现全局+项目)
  skills    = SkillRegistry(发现全局+项目)
  systemPrompt = buildSystemPrompt(kBaseSystemPrompt, skills.list())
  registry.registerBuiltinTools(); registry.registerTool(makeSkillTool(skills))
  router = CommandRouter(commands)

每次输入：
  outcome = router.handle(input, messages)
    Quit       → 退出
    Cleared    → 清屏 + 清 messages（保留 System）
    Prompt     → messages.push(userText(outcome.prompt)); 跑循环
    NotACommand→ messages.push(userText(input));          跑循环

循环内：模型调用 skill(name) → SkillTool 返回 SKILL.md 正文 → 作为 tool_result 回灌继续循环
```

## 错误处理

- 文件/目录缺失或不可读 → 跳过（Rules 段为空、命令/技能不注册），不中断启动。
- frontmatter 解析失败 → 用文件/目录名作 name、空 description，仍可用。
- 未知 `/命令` → `NotACommand`，按普通消息发给模型（与现有一致）。
- `skill` 未知 name → `ToolError`（回灌报错让模型纠正，不崩）。

## 测试（GoogleTest + 临时目录，参照 `ListDirToolTest`）

- **Workspace**：`listMarkdown` 递归 + 排序；`readFile` 存在/缺失。
- **Rules**：全局+项目拼接顺序；`rules/` 多文件排序；缺失文件跳过；`buildSystemPrompt` 组装。
- **CommandRegistry/Router**：发现模板命令；`$ARGUMENTS` 展开；项目覆盖全局；内置 `/quit /clear`
  仍返回 Quit/Cleared；未知命令 → NotACommand。
- **SkillRegistry**：发现 + frontmatter 解析 + 项目覆盖全局。
- **SkillTool**：按 name 返回正文；未知 name 抛 ToolError；`name`、`needsPermission=false`。

## 构建顺序

0. `Workspace` 文件发现工具（+ 单测）
1. **Rules**：`loadRules` + `buildSystemPrompt` + 入口去重接入（+ 单测）
2. **Commands**：`CommandRegistry` + `CommandRouter` 改造 + 调用方接 `Prompt` + 补全菜单数据化（+ 单测）
3. **Skills**：`SkillRegistry` + `SkillTool` + 注册 + system prompt 注入（+ 单测）

每步独立可编译、可测、可跑。

## 不在本设计范围

- 向上逐级目录查找（只做项目 + 全局两层）。
- 命令参数的 `$1..$N` 位置形式（只做 `$ARGUMENTS`）。
- Skill 附带脚本执行（只读 markdown 正文）。
- 命令/技能与权限层的交互（沿用现有 `needsPermission` 机制，本设计不改）。
```
