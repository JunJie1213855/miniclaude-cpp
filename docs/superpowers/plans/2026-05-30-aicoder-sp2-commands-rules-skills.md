# AICoder SP2 — Commands / Rules / Skills Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add file-driven Commands, Rules, and Skills to AICoder — loaded from global `~/.aicoder/` and project `./` (incl. `./.aicoder/`), project overriding global.

**Architecture:** A shared `workspace` file-discovery layer underpins three overlays on the existing loop: Rules concatenate `CLAUDE.md` + `rules/*.md` into the system prompt; Commands extend `CommandRouter` with `.aicoder/commands/*.md` prompt templates (`$ARGUMENTS`); Skills are `.aicoder/skills/<name>/SKILL.md` packs loaded on demand via a `skill` tool. Build order: Workspace → Rules → Commands → Skills.

**Tech Stack:** C++20, CMake (GLOB `CONFIGURE_DEPENDS` — new files auto-included; run `cmake -S . -B build` once after adding files), GoogleTest, nlohmann/json, `std::filesystem`.

**Spec:** `docs/superpowers/specs/2026-05-30-aicoder-sp2-commands-rules-skills-design.md`

---

## File Structure

```
src/workspace/Workspace.{h,cpp}      # readFile, listMarkdown, globalDir, parseFrontmatter
src/rules/Rules.{h,cpp}              # kBaseSystemPrompt, concatSources, loadRules, buildSystemPrompt
src/commands/CommandRegistry.{h,cpp} # discover/find/list template commands
src/commands/CommandRouter.{h,cpp}   # MODIFY: CommandOutcome + Prompt + commands()
src/skills/SkillRegistry.{h,cpp}     # discover/find/list/promptList
src/skills/SkillTool.{h,cpp}         # makeSkillTool(registry)
src/ui/ReplView.{h,cpp}              # MODIFY: data-driven completion list
src/ui/App.cpp                       # MODIFY: handle CommandOutcome, pass command list, wire skill list
src/ui/ConsoleRepl.cpp               # MODIFY: handle CommandOutcome
src/main.cpp, src/main_tui.cpp       # MODIFY: discover + buildSystemPrompt + register skill tool
tests/WorkspaceTest.cpp, RulesTest.cpp, CommandRegistryTest.cpp,
tests/CommandRouterTest.cpp (MODIFY), SkillRegistryTest.cpp, SkillToolTest.cpp
```

All tests use a temp dir (mirror `tests/ListDirToolTest.cpp`). Loaders take explicit paths so they're testable without touching `$HOME`/cwd.

---

## Phase 0 — Workspace (file discovery)

### Task 0.1: readFile + listMarkdown

**Files:**
- Create: `src/workspace/Workspace.h`, `src/workspace/Workspace.cpp`
- Test: `tests/WorkspaceTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/WorkspaceTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "workspace/Workspace.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_ws_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST(Workspace, ReadFilePresentAndMissing) {
  fs::path d = scratch();
  { std::ofstream(d / "a.txt") << "hi"; }
  EXPECT_EQ(readFile(d / "a.txt").value_or("<none>"), "hi");
  EXPECT_FALSE(readFile(d / "nope.txt").has_value());
  fs::remove_all(d);
}

TEST(Workspace, ListMarkdownRecursiveSorted) {
  fs::path d = scratch();
  fs::create_directories(d / "sub");
  { std::ofstream(d / "b.md") << "B"; }
  { std::ofstream(d / "a.md") << "A"; }
  { std::ofstream(d / "sub" / "c.md") << "C"; }
  { std::ofstream(d / "skip.txt") << "X"; }
  auto md = listMarkdown(d);
  ASSERT_EQ(md.size(), 3u);
  EXPECT_EQ(md[0].filename().string(), "a.md");   // sorted by path
  EXPECT_EQ(md[1].filename().string(), "b.md");
  EXPECT_EQ(md[2].filename().string(), "c.md");
  EXPECT_TRUE(listMarkdown(d / "missing").empty());
  fs::remove_all(d);
}
```

- [ ] **Step 2: Run, verify it fails to compile (no header)**

Run: `cmake -S . -B build && cmake --build build --target aicoder_tests`
Expected: FAIL — `workspace/Workspace.h` not found.

- [ ] **Step 3: Create the header**

```cpp
// src/workspace/Workspace.h
#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aicoder {
// 读文件内容；不存在/不可读返回 nullopt。
std::optional<std::string> readFile(const std::filesystem::path& p);
// 递归收集 dir 下所有 *.md，按路径排序；目录不存在返回空。
std::vector<std::filesystem::path> listMarkdown(const std::filesystem::path& dir);
// $HOME/.aicoder（无 HOME 返回空 path）。
std::filesystem::path globalDir();

// 解析 markdown 顶部 frontmatter（--- 块内的 key: value），返回元数据 + 去掉 frontmatter 的正文。
struct Frontmatter { std::map<std::string, std::string> meta; std::string body; };
Frontmatter parseFrontmatter(const std::string& content);
}
```

- [ ] **Step 4: Implement readFile + listMarkdown + globalDir**

```cpp
// src/workspace/Workspace.cpp
#include "workspace/Workspace.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace aicoder {
namespace fs = std::filesystem;

std::optional<std::string> readFile(const fs::path& p) {
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) return std::nullopt;
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<fs::path> listMarkdown(const fs::path& dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return out;
  for (auto it = fs::recursive_directory_iterator(
           dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec) && it->path().extension() == ".md")
      out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

fs::path globalDir() {
  const char* home = std::getenv("HOME");
  if (!home || !*home) return {};
  return fs::path(home) / ".aicoder";
}
}  // namespace aicoder
```

- [ ] **Step 5: Run tests, verify pass**

Run: `cmake --build build --target aicoder_tests && ./build/aicoder_tests --gtest_filter='Workspace.*'`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add src/workspace/Workspace.h src/workspace/Workspace.cpp tests/WorkspaceTest.cpp
git commit -m "feat: add workspace file-discovery (readFile, listMarkdown, globalDir)"
```

### Task 0.2: parseFrontmatter

**Files:** Modify `src/workspace/Workspace.cpp`; Test `tests/WorkspaceTest.cpp`

- [ ] **Step 1: Add failing test**

```cpp
TEST(Workspace, ParseFrontmatter) {
  auto fm = parseFrontmatter("---\nname: foo\ndescription: does X\n---\nbody line\n");
  EXPECT_EQ(fm.meta["name"], "foo");
  EXPECT_EQ(fm.meta["description"], "does X");
  EXPECT_EQ(fm.body, "body line\n");

  auto plain = parseFrontmatter("no frontmatter here");
  EXPECT_TRUE(plain.meta.empty());
  EXPECT_EQ(plain.body, "no frontmatter here");
}
```

- [ ] **Step 2: Run, verify fail** — `./build/aicoder_tests --gtest_filter='Workspace.ParseFrontmatter'` → FAIL (undefined `parseFrontmatter`).

- [ ] **Step 3: Implement** (append to `Workspace.cpp`, add `#include <cctype>`)

```cpp
namespace {
std::string trimWs(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}
}

Frontmatter parseFrontmatter(const std::string& content) {
  Frontmatter fm;
  if (content.rfind("---\n", 0) != 0) { fm.body = content; return fm; }
  size_t end = content.find("\n---", 3);
  if (end == std::string::npos) { fm.body = content; return fm; }
  std::string block = content.substr(4, end - 4);
  size_t bodyStart = content.find('\n', end + 1);
  fm.body = (bodyStart == std::string::npos) ? "" : content.substr(bodyStart + 1);
  std::istringstream is(block);
  std::string line;
  while (std::getline(is, line)) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    fm.meta[trimWs(line.substr(0, colon))] = trimWs(line.substr(colon + 1));
  }
  return fm;
}
```

- [ ] **Step 4: Run tests, verify pass** — filter `Workspace.*` → PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/workspace/Workspace.cpp tests/WorkspaceTest.cpp
git commit -m "feat: add frontmatter parser to workspace"
```

---

## Phase 1 — Rules

### Task 1.1: concatSources + loadRules + buildSystemPrompt

**Files:**
- Create: `src/rules/Rules.h`, `src/rules/Rules.cpp`, `tests/RulesTest.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/RulesTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "rules/Rules.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_rules_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST(Rules, ConcatGlobalThenProjectFilesAndDirs) {
  fs::path root = scratch();
  fs::path g = root / "global", p = root / "proj";
  fs::create_directories(g / "rules");
  fs::create_directories(p / ".aicoder" / "rules");
  { std::ofstream(g / "CLAUDE.md") << "GLOBAL_CLAUDE"; }
  { std::ofstream(g / "rules" / "a.md") << "GLOBAL_RULE_A"; }
  { std::ofstream(p / "CLAUDE.md") << "PROJ_CLAUDE"; }
  { std::ofstream(p / ".aicoder" / "rules" / "z.md") << "PROJ_RULE_Z"; }

  std::string r = loadRules(g, p);
  // 顺序：全局 CLAUDE → 全局 rules → 项目 CLAUDE → 项目 rules
  auto pos = [&](const std::string& s) { return r.find(s); };
  EXPECT_NE(pos("GLOBAL_CLAUDE"), std::string::npos);
  EXPECT_LT(pos("GLOBAL_CLAUDE"), pos("GLOBAL_RULE_A"));
  EXPECT_LT(pos("GLOBAL_RULE_A"), pos("PROJ_CLAUDE"));
  EXPECT_LT(pos("PROJ_CLAUDE"), pos("PROJ_RULE_Z"));
  fs::remove_all(root);
}

TEST(Rules, MissingSourcesSkipped) {
  fs::path root = scratch();
  EXPECT_EQ(loadRules(root / "nope_g", root / "nope_p"), "");
  fs::remove_all(root);
}

TEST(Rules, BuildSystemPromptOmitsEmptySections) {
  EXPECT_EQ(buildSystemPrompt("BASE", "", ""), "BASE");
  EXPECT_NE(buildSystemPrompt("BASE", "RULES", "").find("RULES"), std::string::npos);
  std::string full = buildSystemPrompt("BASE", "RULES", "- s: d");
  EXPECT_NE(full.find("## 可用技能"), std::string::npos);
  EXPECT_NE(full.find("- s: d"), std::string::npos);
}
```

- [ ] **Step 2: Run, verify fail** — no `rules/Rules.h`.

- [ ] **Step 3: Create header**

```cpp
// src/rules/Rules.h
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace aicoder {
extern const char* const kBaseSystemPrompt;
// 按序拼接：文件→内容；目录→递归 *.md（排序）。各非空段以空行分隔，缺失跳过。
std::string concatSources(const std::vector<std::filesystem::path>& sources);
// 全局(globalDir=~/.aicoder) + 项目(projectRoot=cwd) 的 Rules 拼接。
std::string loadRules(const std::filesystem::path& globalDir,
                      const std::filesystem::path& projectRoot);
// base + rules 段 + 「## 可用技能」段；任一为空则省略该段。
std::string buildSystemPrompt(const std::string& base, const std::string& rules,
                              const std::string& skillList);
}
```

- [ ] **Step 4: Implement**

```cpp
// src/rules/Rules.cpp
#include "rules/Rules.h"
#include "workspace/Workspace.h"

namespace aicoder {
namespace fs = std::filesystem;

// 把 main.cpp/main_tui.cpp 现有的硬编码 systemPrompt R"(...)" 原样移到这里。
const char* const kBaseSystemPrompt = R"(<<< MOVE THE EXISTING systemPrompt R-STRING FROM src/main.cpp (你是 AICoder ... 代码示例) HERE VERBATIM >>>)";

std::string concatSources(const std::vector<fs::path>& sources) {
  std::string out;
  auto append = [&](const std::string& s) {
    if (s.empty()) return;
    if (!out.empty()) out += "\n\n";
    out += s;
  };
  for (const auto& src : sources) {
    std::error_code ec;
    if (fs::is_directory(src, ec)) {
      for (const auto& md : listMarkdown(src))
        if (auto c = readFile(md)) append(*c);
    } else if (auto c = readFile(src)) {
      append(*c);
    }
  }
  return out;
}

std::string loadRules(const fs::path& globalDir, const fs::path& projectRoot) {
  return concatSources({
      globalDir / "CLAUDE.md",
      globalDir / "rules",
      projectRoot / "CLAUDE.md",
      projectRoot / ".aicoder" / "rules",
  });
}

std::string buildSystemPrompt(const std::string& base, const std::string& rules,
                              const std::string& skillList) {
  std::string out = base;
  if (!rules.empty()) out += "\n\n" + rules;
  if (!skillList.empty()) out += "\n\n## 可用技能\n" + skillList;
  return out;
}
}  // namespace aicoder
```

> NOTE for Step 4: open `src/main.cpp`, cut the exact `R"(...)"` assigned to `systemPrompt` (the `你是 AICoder...` block), and paste it as the value of `kBaseSystemPrompt`. Do not paraphrase.

- [ ] **Step 5: Run tests, verify pass** — `./build/aicoder_tests --gtest_filter='Rules.*'` → PASS (3).

- [ ] **Step 6: Commit**

```bash
git add src/rules/Rules.h src/rules/Rules.cpp tests/RulesTest.cpp
git commit -m "feat: add Rules loader (CLAUDE.md + rules/*.md) and buildSystemPrompt"
```

### Task 1.2: Wire Rules into both entrypoints (dedupe system prompt)

**Files:** Modify `src/main.cpp`, `src/main_tui.cpp`

- [ ] **Step 1: Replace the hardcoded `systemPrompt` block in `src/main.cpp`**

Remove the local `std::string systemPrompt = R"(...)";` block (now in `kBaseSystemPrompt`). Add include `#include "rules/Rules.h"` and `#include "workspace/Workspace.h"` and `#include <filesystem>`. Build the prompt:

```cpp
  std::string systemPrompt = buildSystemPrompt(
      kBaseSystemPrompt,
      loadRules(globalDir(), std::filesystem::current_path()),
      /*skillList=*/"");   // filled in Phase 3
```

- [ ] **Step 2: Apply the identical change to `src/main_tui.cpp`** (same three includes, same replacement).

- [ ] **Step 3: Build, verify green**

Run: `cmake --build build`
Expected: `aicoder`, `aicoder_tui`, `aicoder_tests` all built.

- [ ] **Step 4: Smoke-test rules load (console, no API call needed for /quit)**

```bash
mkdir -p /tmp/aicoder_rule_demo && cd /tmp/aicoder_rule_demo && echo "PROJECT RULE MARKER" > CLAUDE.md
printf '/quit\n' | AICODER_API_KEY=dummy <repo>/build/aicoder   # exits cleanly; rule was loaded into prompt at startup
cd - && rm -rf /tmp/aicoder_rule_demo
```
Expected: banner + clean exit (exit 0).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/main_tui.cpp
git commit -m "feat: load CLAUDE.md/rules into system prompt; dedupe base prompt"
```

---

## Phase 2 — Commands

### Task 2.1: CommandRegistry

**Files:** Create `src/commands/CommandRegistry.h`, `src/commands/CommandRegistry.cpp`, `tests/CommandRegistryTest.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/CommandRegistryTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "commands/CommandRegistry.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_cmdreg_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST(CommandRegistry, DiscoversAndProjectOverridesGlobal) {
  fs::path root = scratch();
  fs::path g = root / "g", p = root / "p";
  fs::create_directories(g); fs::create_directories(p);
  { std::ofstream(g / "review.md") << "---\ndescription: global review\n---\nGLOBAL BODY $ARGUMENTS"; }
  { std::ofstream(g / "explain.md") << "explain body"; }
  { std::ofstream(p / "review.md") << "---\ndescription: project review\n---\nPROJECT BODY $ARGUMENTS"; }

  CommandRegistry reg;
  reg.discover(g, p);
  ASSERT_NE(reg.find("review"), nullptr);
  EXPECT_EQ(reg.find("review")->description, "project review");      // project wins
  EXPECT_NE(reg.find("review")->body.find("PROJECT BODY"), std::string::npos);
  EXPECT_NE(reg.find("explain"), nullptr);                            // global-only kept
  EXPECT_EQ(reg.find("missing"), nullptr);
  EXPECT_EQ(reg.list().size(), 2u);
  fs::remove_all(root);
}
```

- [ ] **Step 2: Run, verify fail** — no header.

- [ ] **Step 3: Create header**

```cpp
// src/commands/CommandRegistry.h
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aicoder {
struct CommandTemplate { std::string name; std::string description; std::string body; };

class CommandRegistry {
public:
  // 从 globalDir + projectDir 发现 *.md；文件名(无扩展)为命令名；项目同名覆盖全局。
  void discover(const std::filesystem::path& globalDir,
                const std::filesystem::path& projectDir);
  const CommandTemplate* find(const std::string& name) const;  // 不存在返回 nullptr
  std::vector<CommandTemplate> list() const;                   // 按名排序
private:
  std::map<std::string, CommandTemplate> templates_;
};
}
```

- [ ] **Step 4: Implement**

```cpp
// src/commands/CommandRegistry.cpp
#include "commands/CommandRegistry.h"
#include "workspace/Workspace.h"

namespace aicoder {
namespace fs = std::filesystem;

namespace {
void loadDir(const fs::path& dir, std::map<std::string, CommandTemplate>& out) {
  for (const auto& md : listMarkdown(dir)) {
    auto content = readFile(md);
    if (!content) continue;
    Frontmatter fm = parseFrontmatter(*content);
    CommandTemplate t;
    t.name = md.stem().string();
    t.description = fm.meta.count("description") ? fm.meta.at("description") : "";
    t.body = fm.body;
    out[t.name] = std::move(t);  // 后写覆盖：项目目录后加载即覆盖全局
  }
}
}

void CommandRegistry::discover(const fs::path& globalDir, const fs::path& projectDir) {
  templates_.clear();
  loadDir(globalDir, templates_);   // 先全局
  loadDir(projectDir, templates_);  // 后项目（覆盖同名）
}

const CommandTemplate* CommandRegistry::find(const std::string& name) const {
  auto it = templates_.find(name);
  return it == templates_.end() ? nullptr : &it->second;
}

std::vector<CommandTemplate> CommandRegistry::list() const {
  std::vector<CommandTemplate> out;
  for (const auto& [k, v] : templates_) out.push_back(v);
  return out;  // std::map 已按 key 排序
}
}  // namespace aicoder
```

- [ ] **Step 5: Run, verify pass** — `--gtest_filter='CommandRegistry.*'` → PASS.

- [ ] **Step 6: Commit**

```bash
git add src/commands/CommandRegistry.h src/commands/CommandRegistry.cpp tests/CommandRegistryTest.cpp
git commit -m "feat: add CommandRegistry (template-command discovery, project overrides global)"
```

### Task 2.2: CommandRouter — CommandOutcome + Prompt + commands()

**Files:** Modify `src/commands/CommandRouter.h`, `src/commands/CommandRouter.cpp`, `tests/CommandRouterTest.cpp`

- [ ] **Step 1: Add failing tests** (append to `tests/CommandRouterTest.cpp`; existing tests must be updated to use `.result` — see Step 4)

```cpp
#include "commands/CommandRegistry.h"
#include <filesystem>
#include <fstream>

TEST(CommandRouter, TemplateCommandExpandsArguments) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_tmpl";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  { std::ofstream(d / "g" / "review.md") << "Please review: $ARGUMENTS"; }
  CommandRegistry reg; reg.discover(d / "g", d / "p");
  CommandRouter r(reg);
  std::vector<Message> msgs;
  auto out = r.handle("/review src/foo.cpp", msgs);
  EXPECT_EQ(out.result, CommandResult::Prompt);
  EXPECT_EQ(out.prompt, "Please review: src/foo.cpp");
  fs::remove_all(d);
}

TEST(CommandRouter, UnknownSlashIsNotACommand) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/nope", msgs).result, CommandResult::NotACommand);
}

TEST(CommandRouter, ListsBuiltinsPlusTemplates) {
  namespace fs = std::filesystem;
  fs::path d = fs::temp_directory_path() / "aicoder_router_list";
  fs::remove_all(d); fs::create_directories(d / "g"); fs::create_directories(d / "p");
  { std::ofstream(d / "g" / "review.md") << "x"; }
  CommandRegistry reg; reg.discover(d / "g", d / "p");
  CommandRouter r(reg);
  bool hasQuit = false, hasReview = false;
  for (const auto& c : r.commands()) {
    if (c.name == "/quit") hasQuit = true;
    if (c.name == "/review") hasReview = true;
  }
  EXPECT_TRUE(hasQuit);
  EXPECT_TRUE(hasReview);
  fs::remove_all(d);
}
```

- [ ] **Step 2: Run, verify fail** — `CommandOutcome`/`Prompt`/`commands()` undefined.

- [ ] **Step 3: Rewrite the header**

```cpp
// src/commands/CommandRouter.h
#pragma once
#include <string>
#include <vector>
#include "core/Message.h"
#include "commands/CommandRegistry.h"

namespace aicoder {
enum class CommandResult { Quit, Cleared, Prompt, NotACommand };
struct CommandOutcome { CommandResult result; std::string prompt; };
// 用于补全菜单：命令名（含前导 /）+ 描述。
struct CommandInfo { std::string name; std::string description; };

class CommandRouter {
public:
  CommandRouter() = default;
  explicit CommandRouter(CommandRegistry registry);
  // 内置 /quit /exit /clear；其余以 / 开头且命中模板命令 → Prompt(展开)；否则 NotACommand。
  CommandOutcome handle(const std::string& input, std::vector<Message>& messages) const;
  // 内置 + 模板命令的清单（供补全菜单）。
  std::vector<CommandInfo> commands() const;
private:
  CommandRegistry registry_;
};
}
```

- [ ] **Step 4: Rewrite the implementation**

```cpp
// src/commands/CommandRouter.cpp
#include "commands/CommandRouter.h"

namespace aicoder {

CommandRouter::CommandRouter(CommandRegistry registry) : registry_(std::move(registry)) {}

namespace {
std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}
std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
    s.replace(p, from.size(), to);
  return s;
}
}

CommandOutcome CommandRouter::handle(const std::string& input,
                                     std::vector<Message>& messages) const {
  std::string cmd = trim(input);
  if (cmd == "/quit" || cmd == "/exit") return {CommandResult::Quit, ""};
  if (cmd == "/clear") {
    std::vector<Message> kept;
    for (const auto& m : messages)
      if (m.role == Role::System) kept.push_back(m);
    messages = std::move(kept);
    return {CommandResult::Cleared, ""};
  }
  if (!cmd.empty() && cmd[0] == '/') {
    size_t sp = cmd.find(' ');
    std::string name = cmd.substr(1, (sp == std::string::npos ? cmd.size() : sp) - 1);
    std::string args = (sp == std::string::npos) ? "" : trim(cmd.substr(sp + 1));
    if (const CommandTemplate* t = registry_.find(name))
      return {CommandResult::Prompt, replaceAll(t->body, "$ARGUMENTS", args)};
  }
  return {CommandResult::NotACommand, ""};
}

std::vector<CommandInfo> CommandRouter::commands() const {
  std::vector<CommandInfo> out = {
      {"/quit", "退出"}, {"/clear", "清空对话"}};
  for (const auto& t : registry_.list())
    out.push_back({"/" + t.name, t.description});
  return out;
}
}  // namespace aicoder
```

- [ ] **Step 5: Update the THREE existing CommandRouter tests** in `tests/CommandRouterTest.cpp` to read `.result`:
  - `r.handle("/quit", msgs)` → `r.handle("/quit", msgs).result`
  - `r.handle("/clear", msgs)` → `.result`
  - `r.handle("hello", msgs)` → `.result`
  - `r.handle("/exit", msgs)` → `.result`
  - the whitespace test cases → append `.result`

- [ ] **Step 6: Run, verify pass** — `--gtest_filter='CommandRouter.*'` → PASS (all).

- [ ] **Step 7: Commit**

```bash
git add src/commands/CommandRouter.h src/commands/CommandRouter.cpp tests/CommandRouterTest.cpp
git commit -m "feat: CommandRouter returns CommandOutcome with Prompt templates + commands() list"
```

### Task 2.3: Wire commands into App + ConsoleRepl + ReplView completion

**Files:** Modify `src/ui/App.cpp`, `src/ui/ConsoleRepl.cpp`, `src/ui/ReplView.{h,cpp}`, `src/main.cpp`, `src/main_tui.cpp`

- [ ] **Step 1: ReplView — data-driven completion list**

In `src/ui/ReplView.h` add to the public API:
```cpp
  // 设置斜杠命令补全清单（由 App 从 CommandRouter::commands() 提供）。
  void setCommands(std::vector<std::pair<std::string, std::string>> commands);  // {name, desc}
```
In `src/ui/ReplView.cpp`:
- Add member `std::vector<std::pair<std::string,std::string>> commands_;` to `Impl`.
- Implement `void ReplView::setCommands(std::vector<std::pair<std::string,std::string>> c) { impl_->commands_ = std::move(c); }`.
- In `completions()`, replace iteration over `kSlashCommands` with iteration over `commands_` (use `.first` as name, `.second` as desc). Delete the hardcoded `kSlashCommands` array and the `SlashCommand` struct; change `completions()` return type to `std::vector<std::pair<std::string,std::string>>` (copy matched entries) and update the render + Tab/Enter handlers to use `.first`/`.second`.

> Mirror the existing completion logic exactly; only the data source changes from `kSlashCommands` to `commands_`.

- [ ] **Step 2: App — handle CommandOutcome + provide command list**

In `src/ui/App.cpp` `onSubmit`, replace the command handling head:
```cpp
      auto outcome = router.handle(input, messages);
      if (outcome.result == CommandResult::Quit) { screen.Exit(); return; }
      if (outcome.result == CommandResult::Cleared) {
        replView.clearMessages();
        replView.appendMessage({"[对话已清空]", false, false});
        return;
      }
      std::string toSend = (outcome.result == CommandResult::Prompt) ? outcome.prompt : input;
      messages.push_back(userText(toSend));
      replView.appendMessage({input, true, false});   // 显示用户输入的原文（含 /command）
      replView.setThinking(true);
```
(The worker thread below is unchanged; it already runs on `messages`.)

In the `App::Impl` constructor, after `replView.setScreen(&screen);` add:
```cpp
    std::vector<std::pair<std::string, std::string>> cmds;
    for (const auto& c : router.commands()) cmds.push_back({c.name, c.description});
    replView.setCommands(std::move(cmds));
```

- [ ] **Step 3: ConsoleRepl — handle CommandOutcome**

In `src/ui/ConsoleRepl.cpp` `run()`, replace the command branch:
```cpp
    CommandOutcome cr = router_.handle(line, messages_);
    if (cr.result == CommandResult::Quit) break;
    if (cr.result == CommandResult::Cleared) { out_ << "[已清空对话]\n"; continue; }
    messages_.push_back(userText(cr.result == CommandResult::Prompt ? cr.prompt : line));
    try { ... loop_.run(messages_) ... }  // unchanged body
```
(Remove the old `messages_.push_back(userText(line));` that preceded the try.)

- [ ] **Step 4: main.cpp / main_tui.cpp — discover commands, build router**

Replace `CommandRouter router;` with:
```cpp
  CommandRegistry commandReg;
  commandReg.discover(globalDir() / "commands",
                      std::filesystem::current_path() / ".aicoder" / "commands");
  CommandRouter router(std::move(commandReg));
```
Add `#include "commands/CommandRegistry.h"`.

- [ ] **Step 5: Build, verify green** — `cmake -S . -B build && cmake --build build` → all targets built.

- [ ] **Step 6: Run full test suite** — `./build/aicoder_tests` → all PASS (no regression).

- [ ] **Step 7: Commit**

```bash
git add src/ui/App.cpp src/ui/ConsoleRepl.cpp src/ui/ReplView.h src/ui/ReplView.cpp src/main.cpp src/main_tui.cpp
git commit -m "feat: wire template commands into App/ConsoleRepl; data-driven completion menu"
```

---

## Phase 3 — Skills

### Task 3.1: SkillRegistry

**Files:** Create `src/skills/SkillRegistry.h`, `src/skills/SkillRegistry.cpp`, `tests/SkillRegistryTest.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/SkillRegistryTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "skills/SkillRegistry.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_skillreg_test";
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
void writeSkill(const fs::path& base, const std::string& name, const std::string& desc,
                const std::string& body) {
  fs::create_directories(base / name);
  std::ofstream(base / name / "SKILL.md")
      << "---\nname: " << name << "\ndescription: " << desc << "\n---\n" << body;
}
}

TEST(SkillRegistry, DiscoverParseAndProjectOverride) {
  fs::path root = scratch();
  fs::path g = root / "g", p = root / "p";
  writeSkill(g, "fmt", "global fmt", "GLOBAL FMT BODY");
  writeSkill(g, "test", "run tests", "TEST BODY");
  writeSkill(p, "fmt", "project fmt", "PROJECT FMT BODY");

  SkillRegistry reg;
  reg.discover(g, p);
  ASSERT_NE(reg.find("fmt"), nullptr);
  EXPECT_EQ(reg.find("fmt")->description, "project fmt");                 // project wins
  EXPECT_EQ(reg.find("fmt")->body, "PROJECT FMT BODY");
  EXPECT_NE(reg.find("test"), nullptr);
  EXPECT_EQ(reg.find("missing"), nullptr);
  EXPECT_NE(reg.promptList().find("fmt: project fmt"), std::string::npos);
  fs::remove_all(root);
}
```

- [ ] **Step 2: Run, verify fail** — no header.

- [ ] **Step 3: Create header**

```cpp
// src/skills/SkillRegistry.h
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aicoder {
struct SkillInfo { std::string name; std::string description; std::string body; };

class SkillRegistry {
public:
  // 从 globalDir/<name>/SKILL.md 与 projectDir/<name>/SKILL.md 发现；项目同名覆盖全局。
  void discover(const std::filesystem::path& globalDir,
                const std::filesystem::path& projectDir);
  const SkillInfo* find(const std::string& name) const;  // 不存在返回 nullptr
  std::vector<SkillInfo> list() const;                   // 按名排序
  std::string promptList() const;  // "- name: description\n" 多行；无技能返回 ""
private:
  std::map<std::string, SkillInfo> skills_;
};
}
```

- [ ] **Step 4: Implement**

```cpp
// src/skills/SkillRegistry.cpp
#include "skills/SkillRegistry.h"
#include "workspace/Workspace.h"

namespace aicoder {
namespace fs = std::filesystem;

namespace {
void loadDir(const fs::path& dir, std::map<std::string, SkillInfo>& out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    auto content = readFile(it->path() / "SKILL.md");
    if (!content) continue;
    Frontmatter fm = parseFrontmatter(*content);
    SkillInfo s;
    s.name = fm.meta.count("name") ? fm.meta.at("name") : it->path().filename().string();
    s.description = fm.meta.count("description") ? fm.meta.at("description") : "";
    s.body = fm.body;
    out[s.name] = std::move(s);
  }
}
}

void SkillRegistry::discover(const fs::path& globalDir, const fs::path& projectDir) {
  skills_.clear();
  loadDir(globalDir, skills_);
  loadDir(projectDir, skills_);  // 项目覆盖同名
}

const SkillInfo* SkillRegistry::find(const std::string& name) const {
  auto it = skills_.find(name);
  return it == skills_.end() ? nullptr : &it->second;
}

std::vector<SkillInfo> SkillRegistry::list() const {
  std::vector<SkillInfo> out;
  for (const auto& [k, v] : skills_) out.push_back(v);
  return out;
}

std::string SkillRegistry::promptList() const {
  std::string out;
  for (const auto& [k, v] : skills_)
    out += "- " + v.name + ": " + v.description + "\n";
  return out;
}
}  // namespace aicoder
```

- [ ] **Step 5: Run, verify pass** — `--gtest_filter='SkillRegistry.*'` → PASS.

- [ ] **Step 6: Commit**

```bash
git add src/skills/SkillRegistry.h src/skills/SkillRegistry.cpp tests/SkillRegistryTest.cpp
git commit -m "feat: add SkillRegistry (SKILL.md discovery, project overrides global)"
```

### Task 3.2: SkillTool

**Files:** Create `src/skills/SkillTool.h`, `src/skills/SkillTool.cpp`, `tests/SkillToolTest.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/SkillToolTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "skills/SkillTool.h"
#include "skills/SkillRegistry.h"
#include "core/Errors.h"
using namespace aicoder;
namespace fs = std::filesystem;

TEST(SkillTool, ReturnsBodyAndErrorsOnUnknown) {
  fs::path root = fs::temp_directory_path() / "aicoder_skilltool_test";
  fs::remove_all(root);
  fs::create_directories(root / "g" / "fmt");
  std::ofstream(root / "g" / "fmt" / "SKILL.md")
      << "---\nname: fmt\ndescription: d\n---\nFORMAT INSTRUCTIONS";
  SkillRegistry reg; reg.discover(root / "g", root / "p");
  Tool t = makeSkillTool(reg);
  EXPECT_EQ(t.name, "skill");
  EXPECT_FALSE(t.needsPermission);
  EXPECT_NE(t.execute(json{{"name", "fmt"}}).find("FORMAT INSTRUCTIONS"), std::string::npos);
  EXPECT_THROW(t.execute(json{{"name", "nope"}}), ToolError);
  fs::remove_all(root);
}
```

- [ ] **Step 2: Run, verify fail** — no header.

- [ ] **Step 3: Create header**

```cpp
// src/skills/SkillTool.h
#pragma once
#include "core/Tool.h"
#include "skills/SkillRegistry.h"

namespace aicoder {
// 创建 skill 工具：模型以 {name} 调用，返回对应 SKILL.md 正文作为指令。
// 注意：reg 必须在工具生命周期内存活（工具按引用捕获）。
Tool makeSkillTool(const SkillRegistry& reg);
}
```

- [ ] **Step 4: Implement**

```cpp
// src/skills/SkillTool.cpp
#include "skills/SkillTool.h"
#include "core/Errors.h"

namespace aicoder {

Tool makeSkillTool(const SkillRegistry& reg) {
  Tool t;
  t.name = "skill";
  t.description =
      "Load a skill: returns the skill's markdown instructions to follow. "
      "Use when a task matches an available skill (see the system prompt's 可用技能 list).";
  // input_schema 的 name 用已发现技能名做 enum 兜底。
  json names = json::array();
  for (const auto& s : reg.list()) names.push_back(s.name);
  t.input_schema = json{
      {"type", "object"},
      {"properties", {{"name", {{"type", "string"}, {"description", "Skill name"}, {"enum", names}}}}},
      {"required", json::array({"name"})}};
  t.needsPermission = false;
  t.execute = [&reg](const json& input) -> std::string {
    if (!input.contains("name") || !input["name"].is_string())
      throw ToolError("missing skill name");
    std::string name = input["name"].get<std::string>();
    const SkillInfo* s = reg.find(name);
    if (!s) throw ToolError("unknown skill: " + name);
    return s->body;
  };
  return t;
}
}  // namespace aicoder
```

- [ ] **Step 5: Run, verify pass** — `--gtest_filter='SkillTool.*'` → PASS.

- [ ] **Step 6: Commit**

```bash
git add src/skills/SkillTool.h src/skills/SkillTool.cpp tests/SkillToolTest.cpp
git commit -m "feat: add skill tool (loads SKILL.md body on demand)"
```

### Task 3.3: Wire skills into both entrypoints

**Files:** Modify `src/main.cpp`, `src/main_tui.cpp`

- [ ] **Step 1: In `src/main.cpp`**, add includes `#include "skills/SkillRegistry.h"` and `#include "skills/SkillTool.h"`. After building the tool registry, before constructing the system prompt:

```cpp
  static SkillRegistry skillReg;   // static: must outlive the skill tool's reference capture
  skillReg.discover(globalDir() / "skills",
                    std::filesystem::current_path() / ".aicoder" / "skills");
  registry.registerTool(makeSkillTool(skillReg));
```
Then update the prompt to pass the skill list:
```cpp
  std::string systemPrompt = buildSystemPrompt(
      kBaseSystemPrompt,
      loadRules(globalDir(), std::filesystem::current_path()),
      skillReg.promptList());
```

- [ ] **Step 2: Apply the identical change to `src/main_tui.cpp`.**

> `static SkillRegistry` keeps it alive for the program's duration (the `skill` tool captures `&reg`). Both `main`s are short-lived processes, so this is safe and simple.

- [ ] **Step 3: Build, verify green** — `cmake --build build` → all targets built.

- [ ] **Step 4: Run full suite** — `./build/aicoder_tests` → all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/main_tui.cpp
git commit -m "feat: discover skills and register skill tool; inject skill list into prompt"
```

---

## Final Verification (manual, real terminal + API key)

Cannot be automated here (interactive TUI + live model). After all phases:

1. **Rules:** create `./CLAUDE.md` with a distinctive instruction; launch `./build/aicoder_tui`; ask a question whose answer reflects the rule.
2. **Commands:** create `./.aicoder/commands/review.md` containing `Review this code: $ARGUMENTS`; type `/` → menu shows `/review`; Tab to complete; type args; Enter → the expanded prompt runs.
3. **Skills:** create `./.aicoder/skills/greet/SKILL.md` with frontmatter `name: greet` / `description: greet politely` + a body; ask something that should trigger it; confirm the model calls the `skill` tool and follows the loaded instructions.
4. Confirm `/quit` still exits and `/clear` still clears (regression).

---

## Self-Review

- **Spec coverage:** §0 Workspace → Phase 0; §1 Rules → Phase 1; §2 Commands → Phase 2; §3 Skills → Phase 3; data flow → Tasks 1.2/2.3/3.3; error handling → covered in each loader (missing→skip, unknown skill→ToolError); testing → temp-dir tests each phase. ✅
- **Placeholders:** the only deliberate "move existing content" reference is `kBaseSystemPrompt` (Task 1.1 Step 4 note) — instruction is explicit (move the exact R-string), not a TODO. ✅
- **Type consistency:** `CommandOutcome{result, prompt}`, `CommandResult::{Quit,Cleared,Prompt,NotACommand}`, `CommandInfo{name,description}`, `CommandTemplate{name,description,body}`, `SkillInfo{name,description,body}`, `parseFrontmatter→Frontmatter{meta,body}` used consistently across tasks. `commands()` returns `CommandInfo` (App maps to `pair<string,string>` for `ReplView::setCommands`). ✅
```
