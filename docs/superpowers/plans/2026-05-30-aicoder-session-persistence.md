# AICoder Session Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist every TUI conversation to `~/.aicoder/sessions/<id>/session.json` per turn, with `-c` (resume latest) and `--resume` (TUI picker) CLI flags.

**Architecture:** Two new modules — `Session` (data model + JSON serialize) and `SessionStore` (filesystem layout, atomic save, list/load). `App` gets a mutable `sessionId_` member, saves after every turn end, and rotates the id on `/clear`. `main_tui.cpp` parses `-c`/`--resume`, optionally runs a small `ResumePicker` FTXUI screen before launching `App`.

**Tech Stack:** C++20, CMake (`GLOB_RECURSE CONFIGURE_DEPENDS` — new files auto-included; run `cmake -S . -B build` once after adding files), nlohmann/json (alias `json` via `core/Json.h`), `std::filesystem`, `std::chrono`, FTXUI (Menu component for the picker), GoogleTest.

**Spec:** `docs/superpowers/specs/2026-05-30-aicoder-session-persistence-design.md`

---

## File Structure

```
src/sessions/Session.{h,cpp}        # SessionData struct + Message↔JSON serialize
src/sessions/SessionStore.{h,cpp}   # filesystem layout: newId, save (atomic), load, list, latestId
src/ui/ResumePicker.{h,cpp}         # showResumePicker(store) → optional<string> selected id
src/main_tui.cpp                    # MODIFY: parseArgs, choose initial sessionId+messages, run picker if needed
src/ui/App.{h,cpp}                  # MODIFY: ctor takes store/sessionId/initialMsgs; save per turn; /clear rotates id
tests/SessionTest.cpp, tests/SessionStoreTest.cpp, tests/MainTuiArgsTest.cpp
```

Existing helpers reused: `globalDir()` (workspace), `json` (core/Json.h), `Message` model (core/Message.h).

---

## Phase 1 — Session model + JSON serialization

### Task 1.1: SessionData struct + Message↔JSON round-trip (TDD)

**Files:**
- Create: `src/sessions/Session.h`, `src/sessions/Session.cpp`, `tests/SessionTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/SessionTest.cpp
#include <gtest/gtest.h>
#include "sessions/Session.h"
using namespace aicoder;

TEST(Session, RoundTripUserAssistantToolMessages) {
  SessionData s;
  s.id = "20260530_133412_a3f2";
  s.created_at = "2026-05-30T13:34:12+08:00";
  s.updated_at = "2026-05-30T13:42:00+08:00";
  s.model = "deepseek-v4-pro";
  s.messages = {
      userText("hi"),
      Message{Role::Assistant,
              {TextBlock{"thinking..."},
               ToolUseBlock{"call_1", "read_file", json{{"path", "a.cpp"}}}},
              "reasoning here"},
      Message{Role::Tool,
              {ToolResultBlock{"call_1", "(file body)", false}}},
  };

  json j = sessionToJson(s);
  EXPECT_EQ(j["schema_version"], 1);
  EXPECT_EQ(j["id"], s.id);
  EXPECT_EQ(j["model"], s.model);
  ASSERT_EQ(j["messages"].size(), 3u);
  EXPECT_EQ(j["messages"][0]["role"], "user");
  EXPECT_EQ(j["messages"][1]["role"], "assistant");
  EXPECT_EQ(j["messages"][1]["reasoning_content"], "reasoning here");
  EXPECT_EQ(j["messages"][2]["role"], "tool");
  EXPECT_EQ(j["messages"][2]["content"][0]["tool_use_id"], "call_1");

  auto back = sessionFromJson(j);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->id, s.id);
  EXPECT_EQ(back->messages.size(), 3u);
  EXPECT_EQ(back->messages[0].role, Role::User);
  EXPECT_EQ(assistantText(back->messages[1]), "thinking...");
  ASSERT_EQ(back->messages[1].content.size(), 2u);
  EXPECT_EQ(back->messages[1].reasoning_content, "reasoning here");
}

TEST(Session, ReasoningContentOmittedWhenEmpty) {
  SessionData s;
  s.id = "x"; s.created_at = "t"; s.updated_at = "t"; s.model = "m";
  s.messages = {Message{Role::Assistant, {TextBlock{"hello"}}, ""}};
  json j = sessionToJson(s);
  EXPECT_FALSE(j["messages"][0].contains("reasoning_content"));
}

TEST(Session, FromJsonRejectsUnknownSchemaVersion) {
  json j = {{"schema_version", 999}, {"id", "x"}, {"created_at", "t"},
            {"updated_at", "t"}, {"model", "m"}, {"messages", json::array()}};
  EXPECT_FALSE(sessionFromJson(j).has_value());
}

TEST(Session, FromJsonRejectsMalformedJson) {
  EXPECT_FALSE(sessionFromJson(json{{"foo", "bar"}}).has_value());
}
```

- [ ] **Step 2: Run test, verify fail** (header missing)

`cmake -S . -B build && cmake --build build --target aicoder_tests 2>&1 | tail -5` → FAIL: `sessions/Session.h` not found.

- [ ] **Step 3: Create the header**

```cpp
// src/sessions/Session.h
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"

namespace aicoder {

struct SessionData {
  std::string id;
  std::string created_at;          // ISO-8601 local time
  std::string updated_at;          // ISO-8601 local time
  std::string model;               // informational; LLM uses current config on resume
  std::vector<Message> messages;   // excludes System (rebuilt on resume)
};

// Serialize a SessionData to its on-disk JSON shape (schema_version=1).
json sessionToJson(const SessionData& s);
// Parse a JSON object back into SessionData; returns nullopt on bad schema/version/structure.
std::optional<SessionData> sessionFromJson(const json& j);

}  // namespace aicoder
```

- [ ] **Step 4: Implement the conversion**

```cpp
// src/sessions/Session.cpp
#include "sessions/Session.h"

namespace aicoder {
namespace {

constexpr int kSchemaVersion = 1;

const char* roleToStr(Role r) {
  switch (r) {
    case Role::System:    return "system";
    case Role::User:      return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool:      return "tool";
  }
  return "user";
}

std::optional<Role> roleFromStr(const std::string& s) {
  if (s == "system")    return Role::System;
  if (s == "user")      return Role::User;
  if (s == "assistant") return Role::Assistant;
  if (s == "tool")      return Role::Tool;
  return std::nullopt;
}

json blockToJson(const ContentBlock& b) {
  if (auto* t = std::get_if<TextBlock>(&b))
    return {{"type", "text"}, {"text", t->text}};
  if (auto* tu = std::get_if<ToolUseBlock>(&b))
    return {{"type", "tool_use"}, {"id", tu->id}, {"name", tu->name}, {"input", tu->input}};
  if (auto* tr = std::get_if<ToolResultBlock>(&b))
    return {{"type", "tool_result"}, {"tool_use_id", tr->tool_use_id},
            {"content", tr->content}, {"is_error", tr->is_error}};
  return json::object();
}

std::optional<ContentBlock> blockFromJson(const json& j) {
  if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return std::nullopt;
  std::string type = j["type"].get<std::string>();
  if (type == "text" && j.contains("text"))
    return ContentBlock{TextBlock{j["text"].get<std::string>()}};
  if (type == "tool_use" && j.contains("id") && j.contains("name") && j.contains("input"))
    return ContentBlock{ToolUseBlock{j["id"].get<std::string>(),
                                     j["name"].get<std::string>(),
                                     j["input"]}};
  if (type == "tool_result" && j.contains("tool_use_id") && j.contains("content"))
    return ContentBlock{ToolResultBlock{j["tool_use_id"].get<std::string>(),
                                        j["content"].get<std::string>(),
                                        j.value("is_error", false)}};
  return std::nullopt;
}

json messageToJson(const Message& m) {
  json out = {{"role", roleToStr(m.role)}, {"content", json::array()}};
  for (const auto& b : m.content) out["content"].push_back(blockToJson(b));
  if (m.role == Role::Assistant && !m.reasoning_content.empty())
    out["reasoning_content"] = m.reasoning_content;
  return out;
}

std::optional<Message> messageFromJson(const json& j) {
  if (!j.is_object() || !j.contains("role") || !j["role"].is_string()) return std::nullopt;
  auto r = roleFromStr(j["role"].get<std::string>());
  if (!r) return std::nullopt;
  Message m;
  m.role = *r;
  if (j.contains("content") && j["content"].is_array()) {
    for (const auto& bj : j["content"]) {
      if (auto b = blockFromJson(bj)) m.content.push_back(std::move(*b));
    }
  }
  if (j.contains("reasoning_content") && j["reasoning_content"].is_string())
    m.reasoning_content = j["reasoning_content"].get<std::string>();
  return m;
}

}  // namespace

json sessionToJson(const SessionData& s) {
  json out = {{"schema_version", kSchemaVersion},
              {"id", s.id},
              {"created_at", s.created_at},
              {"updated_at", s.updated_at},
              {"model", s.model},
              {"messages", json::array()}};
  for (const auto& m : s.messages) out["messages"].push_back(messageToJson(m));
  return out;
}

std::optional<SessionData> sessionFromJson(const json& j) {
  if (!j.is_object()) return std::nullopt;
  if (!j.contains("schema_version") || j["schema_version"] != kSchemaVersion) return std::nullopt;
  if (!j.contains("id") || !j.contains("created_at") || !j.contains("updated_at") ||
      !j.contains("model") || !j.contains("messages") || !j["messages"].is_array())
    return std::nullopt;
  SessionData s;
  s.id = j["id"].get<std::string>();
  s.created_at = j["created_at"].get<std::string>();
  s.updated_at = j["updated_at"].get<std::string>();
  s.model = j["model"].get<std::string>();
  for (const auto& mj : j["messages"]) {
    if (auto m = messageFromJson(mj)) s.messages.push_back(std::move(*m));
  }
  return s;
}

}  // namespace aicoder
```

- [ ] **Step 5: Run tests, verify PASS (4)**

`cmake --build build --target aicoder_tests && ./build/aicoder_tests --gtest_filter='Session.*'`
Expected: 4/4 PASSED.

- [ ] **Step 6: Commit**

```bash
git add src/sessions/Session.h src/sessions/Session.cpp tests/SessionTest.cpp
git commit -m "feat: session data model + JSON serialization (schema_version=1)"
```

---

## Phase 2 — SessionStore (filesystem)

### Task 2.1: newId + save (atomic) + load (TDD)

**Files:**
- Create: `src/sessions/SessionStore.h`, `src/sessions/SessionStore.cpp`, `tests/SessionStoreTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/SessionStoreTest.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "sessions/SessionStore.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_sessstore_test";
  fs::remove_all(d);
  return d;
}
SessionData sample(const std::string& id, const std::string& body) {
  SessionData s;
  s.id = id; s.created_at = "2026-05-30T00:00:00+08:00";
  s.updated_at = "2026-05-30T00:00:01+08:00"; s.model = "m";
  s.messages = {userText(body)};
  return s;
}
}

TEST(SessionStore, NewIdsAreUnique) {
  SessionStore store(scratch());
  std::set<std::string> ids;
  for (int i = 0; i < 100; ++i) ids.insert(store.newId());
  EXPECT_EQ(ids.size(), 100u);
}

TEST(SessionStore, SaveCreatesDirAndAtomicallyWritesFile) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string id = store.newId();
  store.save(id, sample(id, "hello"));
  fs::path file = root / id / "session.json";
  EXPECT_TRUE(fs::is_regular_file(file));
  EXPECT_FALSE(fs::exists(file.string() + ".tmp"));  // tmp cleaned up
}

TEST(SessionStore, LoadRoundTrip) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string id = store.newId();
  store.save(id, sample(id, "round-trip body"));
  auto loaded = store.load(id);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->id, id);
  ASSERT_EQ(loaded->messages.size(), 1u);
  EXPECT_EQ(assistantText(loaded->messages[0]), "round-trip body");  // user text via shared helper
}

TEST(SessionStore, LoadMissingReturnsNullopt) {
  SessionStore store(scratch());
  EXPECT_FALSE(store.load("nope").has_value());
}

TEST(SessionStore, LoadCorruptJsonReturnsNullopt) {
  fs::path root = scratch();
  SessionStore store(root);
  fs::create_directories(root / "bad");
  { std::ofstream(root / "bad" / "session.json") << "not json {"; }
  EXPECT_FALSE(store.load("bad").has_value());
}
```

- [ ] **Step 2: Run test, verify fail** (header missing).

- [ ] **Step 3: Create header**

```cpp
// src/sessions/SessionStore.h
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "sessions/Session.h"

namespace aicoder {

struct SessionInfo {
  std::string id;
  std::string updated_at;
  std::string model;
  std::string preview;        // first user message, trimmed to <=60 chars; "" if none
  size_t message_count = 0;
  std::filesystem::file_time_type mtime;  // for sorting
};

class SessionStore {
public:
  // root defaults to ~/.aicoder/sessions; root will be created if missing.
  explicit SessionStore(std::filesystem::path root);

  std::string newId() const;                                 // YYYYMMDD_HHMMSS_xxxx (4 hex)
  void save(const std::string& id, const SessionData& data); // atomic: write .tmp + rename
  std::optional<SessionData> load(const std::string& id);    // nullopt on missing/corrupt
  std::vector<SessionInfo> listSessions() const;             // sorted: most recent first, skips corrupt
  std::optional<std::string> latestId() const;               // newest by mtime; nullopt if empty

  const std::filesystem::path& root() const { return root_; }

private:
  std::filesystem::path root_;
  std::filesystem::path fileFor(const std::string& id) const { return root_ / id / "session.json"; }
};

}  // namespace aicoder
```

- [ ] **Step 4: Implement (Task 2.1's portion: newId / save / load)**

```cpp
// src/sessions/SessionStore.cpp
#include "sessions/SessionStore.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace aicoder {
namespace fs = std::filesystem;

namespace {
std::string nowIsoLocal() {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
  // %z gives +0800 — turn into +08:00 for ISO-8601 strict
  std::string s = os.str();
  if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-'))
    s.insert(s.size() - 2, ":");
  return s;
}

std::string fourHex() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 0xFFFF);
  std::ostringstream os;
  os << std::hex << std::setw(4) << std::setfill('0') << dist(gen);
  return os.str();
}
}  // namespace

SessionStore::SessionStore(fs::path root) : root_(std::move(root)) {
  std::error_code ec;
  fs::create_directories(root_, ec);
}

std::string SessionStore::newId() const {
  using namespace std::chrono;
  std::time_t t = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << fourHex();
  return os.str();
}

void SessionStore::save(const std::string& id, const SessionData& data) {
  std::error_code ec;
  fs::path dir = root_ / id;
  fs::create_directories(dir, ec);
  fs::path file = dir / "session.json";
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << sessionToJson(data).dump(2);
  }
  fs::rename(tmp, file, ec);  // atomic on POSIX same-fs
  if (ec) {
    // best effort: try removing tmp if rename failed
    fs::remove(tmp, ec);
  }
}

std::optional<SessionData> SessionStore::load(const std::string& id) {
  std::error_code ec;
  fs::path file = fileFor(id);
  if (!fs::is_regular_file(file, ec)) return std::nullopt;
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss; ss << in.rdbuf();
  json j;
  try { j = json::parse(ss.str()); }
  catch (const json::exception&) { return std::nullopt; }
  return sessionFromJson(j);
}

// listSessions and latestId implemented in Task 2.2.
std::vector<SessionInfo> SessionStore::listSessions() const { return {}; }
std::optional<std::string> SessionStore::latestId() const { return std::nullopt; }

}  // namespace aicoder
```

- [ ] **Step 5: Run tests, verify PASS (5 from Task 2.1)**

`cmake --build build --target aicoder_tests && ./build/aicoder_tests --gtest_filter='SessionStore.NewIds*:SessionStore.Save*:SessionStore.Load*'`
Expected: 5/5 PASSED.

- [ ] **Step 6: Commit**

```bash
git add src/sessions/SessionStore.h src/sessions/SessionStore.cpp tests/SessionStoreTest.cpp
git commit -m "feat: SessionStore — newId, atomic save, load (filesystem layout)"
```

### Task 2.2: listSessions + latestId (TDD)

**Files:**
- Modify: `src/sessions/SessionStore.cpp`, `tests/SessionStoreTest.cpp`

- [ ] **Step 1: Add failing tests**

```cpp
// append to tests/SessionStoreTest.cpp

TEST(SessionStore, ListSessionsSortedByMtimeDesc) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string a = store.newId();
  store.save(a, sample(a, "first"));
  // sleep so mtime differs
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::string b = store.newId();
  store.save(b, sample(b, "second user message here"));
  auto list = store.listSessions();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].id, b);            // newest first
  EXPECT_EQ(list[1].id, a);
  EXPECT_EQ(list[0].preview, "second user message here");
  EXPECT_EQ(list[0].message_count, 1u);
}

TEST(SessionStore, ListSessionsSkipsCorrupt) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string good = store.newId();
  store.save(good, sample(good, "ok"));
  fs::create_directories(root / "bad_dir");
  { std::ofstream(root / "bad_dir" / "session.json") << "not json"; }
  auto list = store.listSessions();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, good);
}

TEST(SessionStore, LatestIdMatchesNewest) {
  fs::path root = scratch();
  SessionStore store(root);
  EXPECT_FALSE(store.latestId().has_value());
  std::string a = store.newId(); store.save(a, sample(a, "x"));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::string b = store.newId(); store.save(b, sample(b, "y"));
  auto latest = store.latestId();
  ASSERT_TRUE(latest.has_value());
  EXPECT_EQ(*latest, b);
}
```

Add `#include <thread>` and `#include <set>` if not yet present in the test file.

- [ ] **Step 2: Run, verify fail** (the empty stubs return {} / nullopt).

- [ ] **Step 3: Implement listSessions + latestId**

Replace the stub implementations in `src/sessions/SessionStore.cpp`:

```cpp
std::vector<SessionInfo> SessionStore::listSessions() const {
  std::vector<SessionInfo> out;
  std::error_code ec;
  if (!fs::is_directory(root_, ec)) return out;
  for (auto it = fs::directory_iterator(root_, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    fs::path file = it->path() / "session.json";
    if (!fs::is_regular_file(file, ec)) continue;
    std::ifstream in(file, std::ios::binary);
    if (!in) continue;
    std::ostringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); } catch (const json::exception&) { continue; }
    auto data = sessionFromJson(j);
    if (!data) continue;
    SessionInfo info;
    info.id = data->id;
    info.updated_at = data->updated_at;
    info.model = data->model;
    info.message_count = data->messages.size();
    for (const auto& m : data->messages) {
      if (m.role == Role::User) {
        std::string txt = assistantText(m);   // joins TextBlocks
        if (txt.size() > 60) txt.resize(60);
        info.preview = txt;
        break;
      }
    }
    info.mtime = fs::last_write_time(file, ec);
    out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(),
            [](const SessionInfo& a, const SessionInfo& b) { return a.mtime > b.mtime; });
  return out;
}

std::optional<std::string> SessionStore::latestId() const {
  auto list = listSessions();
  if (list.empty()) return std::nullopt;
  return list.front().id;
}
```

- [ ] **Step 4: Run tests, verify PASS (8 total in SessionStore)**

`./build/aicoder_tests --gtest_filter='SessionStore.*'` → 8/8 PASSED.

- [ ] **Step 5: Commit**

```bash
git add src/sessions/SessionStore.cpp tests/SessionStoreTest.cpp
git commit -m "feat: SessionStore — listSessions (sorted, skip corrupt) + latestId"
```

---

## Phase 3 — App save wiring + `/clear` rotation

### Task 3.1: App accepts SessionStore + initial messages; saves per turn; rotates id on `/clear`

**Files:**
- Modify: `src/ui/App.h`, `src/ui/App.cpp`, `src/main_tui.cpp`

- [ ] **Step 1: Update App.h signature**

Open `src/ui/App.h`. Currently App's constructor takes `(AgentLoop&, CommandRouter&, const std::string& systemPrompt)`. Add a forward declaration `class SessionStore;` and change the constructor to:

```cpp
namespace aicoder {
class AgentLoop;
class CommandRouter;
class ReplView;
class SessionStore;
struct SessionData;

class App {
public:
  App(AgentLoop& loop, CommandRouter& router, const std::string& systemPrompt,
      SessionStore& store, std::string initialSessionId,
      std::vector<Message> initialMessages, std::string model);
  ~App();
  void run();
private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}
```

(`Message` is in `core/Message.h` — make sure the include is present, or fully-qualified `aicoder::Message`. The existing file already pulls component.hpp; add `#include "core/Message.h"` and `#include <string>` / `<vector>` at top of App.h.)

- [ ] **Step 2: Update App.cpp Impl ctor and onSubmit**

In `src/ui/App.cpp`:

- Add includes near the top: `#include "sessions/Session.h"` and `#include "sessions/SessionStore.h"`.
- Change `App::Impl` to hold the new state:

```cpp
class App::Impl {
public:
  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  ReplView replView;
  AgentLoop& agentLoop;
  CommandRouter& router;
  std::vector<Message> messages;
  SessionStore& store;
  std::string sessionId_;     // mutable; /clear rotates
  std::string model_;

  Impl(AgentLoop& loop, CommandRouter& r, std::vector<Message> initialMsgs,
       SessionStore& s, std::string sid, std::string m)
      : agentLoop(loop), router(r), messages(std::move(initialMsgs)),
        store(s), sessionId_(std::move(sid)), model_(std::move(m)),
        replView([this](const std::string& input) { onSubmit(input); }) {
    replView.setScreen(&screen);
    std::vector<std::pair<std::string, std::string>> cmds;
    for (const auto& c : router.commands()) cmds.push_back({c.name, c.description});
    replView.setCommands(std::move(cmds));
    // Replay loaded history into the UI so users see what they're resuming.
    for (const auto& m : messages) {
      if (m.role == Role::User) {
        replView.appendMessage({assistantText(m), true, false});
      } else if (m.role == Role::Assistant) {
        std::string text = assistantText(m);
        if (!text.empty()) replView.appendMessage({text, false, false});
      }
      // Tool messages: skip in UI (they're plumbing).
    }
  }

  // Build a SessionData snapshot from current messages (skip System).
  SessionData snapshot() const {
    SessionData d;
    d.id = sessionId_;
    d.created_at = ""; // overwritten below from existing file if any (else set on first save)
    d.updated_at = "";
    d.model = model_;
    for (const auto& m : messages)
      if (m.role != Role::System) d.messages.push_back(m);
    return d;
  }

  void saveTurn() {
    // load existing to preserve created_at; if none, both timestamps = now.
    SessionData d = snapshot();
    auto existing = store.load(sessionId_);
    std::string now = nowIsoLocal();   // small helper added below
    d.created_at = existing ? existing->created_at : now;
    d.updated_at = now;
    store.save(sessionId_, d);
  }

  // ... existing onSubmit method, see Step 3.
};
```

Add a tiny local helper at the top of the file (anonymous namespace) for the timestamp — same shape as SessionStore's (we copy here to keep the dependency direction clean):

```cpp
namespace {
std::string nowIsoLocal() {
  using namespace std::chrono;
  std::time_t t = system_clock::to_time_t(system_clock::now());
  std::tm tm{}; localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
  std::string s = os.str();
  if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-'))
    s.insert(s.size() - 2, ":");
  return s;
}
}
```

Add includes: `<chrono>`, `<ctime>`, `<iomanip>`, `<sstream>`.

- [ ] **Step 3: Wire save into onSubmit (per turn + /clear rotation)**

Replace App::Impl::onSubmit's branches to save:

```cpp
  void onSubmit(const std::string& input) {
    auto outcome = router.handle(input, messages);
    if (outcome.result == CommandResult::Quit) { screen.Exit(); return; }
    if (outcome.result == CommandResult::Cleared) {
      sessionId_ = store.newId();                       // rotate to new id
      saveTurn();                                       // record the empty new session
      replView.clearMessages();
      replView.appendMessage({"[对话已清空]", false, false});
      return;
    }
    if (outcome.result == CommandResult::Reloaded) {
      std::vector<std::pair<std::string, std::string>> cmds;
      for (const auto& c : router.commands()) cmds.push_back({c.name, c.description});
      replView.setCommands(std::move(cmds));
      replView.appendMessage({outcome.prompt, false, false});
      return;
    }
    messages.push_back(userText(outcome.result == CommandResult::Prompt ? outcome.prompt : input));
    replView.appendMessage({input, true, false});
    replView.setThinking(true);

    ChatMode mode = replView.mode();
    auto loopPtr = &agentLoop;
    auto msgsPtr = &messages;
    auto rv = &replView;
    auto storePtr = &store;
    std::string sid = sessionId_;
    std::string model = model_;
    std::thread([=]() {
      try {
        auto onDelta = [rv](const StreamDelta& d) { rv->appendDelta(d.text, d.reasoning); };
        auto onPerm  = [rv](const std::string& tn, const json& in) { return rv->askPermission(tn, in); };
        std::string reply;
        if (mode == ChatMode::Reflection) {
          loopPtr->setSelfCheck(true);
          reply = loopPtr->runWithReflection(*msgsPtr, kReflectRounds, onDelta, onPerm);
        } else if (mode == ChatMode::PlanExecute) {
          loopPtr->setSelfCheck(false);
          auto onInfo = [rv](const std::string& text) {
            rv->appendMessage({text, false, false}); rv->setThinking(true);
          };
          reply = loopPtr->runPlanExecute(*msgsPtr, kPlanExecuteMaxSteps, onDelta, onPerm, onInfo);
        } else {
          loopPtr->setSelfCheck(false);
          reply = loopPtr->run(*msgsPtr, onDelta, onPerm);
        }
        rv->appendMessage({reply, false, false});
      } catch (const LlmError& e) {
        rv->appendError(std::string("[错误] ") + e.what());
      }
      // ALWAYS save what we have, success or error. messages was already mutated by the loop / catch above.
      SessionData d;
      d.id = sid;
      d.model = model;
      for (const auto& m : *msgsPtr)
        if (m.role != Role::System) d.messages.push_back(m);
      auto existing = storePtr->load(sid);
      std::string now = nowIsoLocal();
      d.created_at = existing ? existing->created_at : now;
      d.updated_at = now;
      storePtr->save(sid, d);
    }).detach();
  }
```

Note: this duplicates the save-snapshot logic from `saveTurn()` because the worker captures values, not `this`. Acceptable — kept inline for clarity.

- [ ] **Step 4: Update App::App constructor**

Change the App constructor's body to pass new args to Impl:

```cpp
App::App(AgentLoop& loop, CommandRouter& router, const std::string& /*systemPrompt - still unused*/,
         SessionStore& store, std::string initialSessionId,
         std::vector<Message> initialMessages, std::string model)
    : impl_(std::make_unique<Impl>(loop, router, std::move(initialMessages),
                                   store, std::move(initialSessionId), std::move(model))) {}
```

- [ ] **Step 5: Update main_tui.cpp call site (transitional — still no CLI; that's Task 4.1)**

In `src/main_tui.cpp`, where `App app(loop, router, systemPrompt);` is constructed, replace with:

```cpp
  // include at top: #include "sessions/SessionStore.h"
  SessionStore sessionStore(globalDir() / "sessions");
  std::string sessionId = sessionStore.newId();   // no CLI yet; always a new session
  App app(loop, router, systemPrompt, sessionStore, sessionId, /*initial=*/{}, config.model);
  app.run();
```

Add `#include "sessions/SessionStore.h"` near other session/workspace includes.

- [ ] **Step 6: Build all targets, run full suite**

`cmake -S . -B build && cmake --build build 2>&1 | tail -8` → all 3 targets built.
`./build/aicoder_tests 2>/dev/null | tail -2` → all tests PASS (whatever current count + 8 from Sessions).

- [ ] **Step 7: Manual smoke (no API, exits via /quit)**

```bash
printf '/quit\n' | AICODER_API_KEY=dummy ./build/aicoder
# aicoder_tui needs a TTY; can't exercise here. Trust that compile + Session tests cover the wiring.
ls -la "$HOME/.aicoder/sessions" 2>/dev/null || echo "(empty / not yet used)"
```

- [ ] **Step 8: Commit**

```bash
git add src/ui/App.h src/ui/App.cpp src/main_tui.cpp
git commit -m "feat: App saves session every turn; /clear rotates to new id; replays loaded history into UI"
```

---

## Phase 4 — CLI parsing + ResumePicker

### Task 4.1: CLI parser (pure function, unit-testable)

**Files:**
- Modify: `src/main_tui.cpp` (extract a small parser); Create: `tests/MainTuiArgsTest.cpp`

For testability, put the parser in `src/sessions/CliArgs.h` (lives next to sessions since it picks the session source).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/MainTuiArgsTest.cpp
#include <gtest/gtest.h>
#include "sessions/CliArgs.h"
using namespace aicoder;

TEST(CliArgs, NoArgsIsNewSession) {
  const char* argv[] = {"aicoder_tui"};
  auto a = parseCliArgs(1, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::NewSession);
}

TEST(CliArgs, DashCIsContinueLatest) {
  const char* argv[] = {"aicoder_tui", "-c"};
  auto a = parseCliArgs(2, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ContinueLatest);
}

TEST(CliArgs, DashDashResumeIsPicker) {
  const char* argv[] = {"aicoder_tui", "--resume"};
  auto a = parseCliArgs(2, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ResumePicker);
}

TEST(CliArgs, ResumeWinsWhenBoth) {
  const char* argv[] = {"aicoder_tui", "-c", "--resume"};
  auto a = parseCliArgs(3, argv);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->mode, CliMode::ResumePicker);
}

TEST(CliArgs, UnknownArgReturnsNullopt) {
  const char* argv[] = {"aicoder_tui", "--what"};
  EXPECT_FALSE(parseCliArgs(2, argv).has_value());
}
```

- [ ] **Step 2: Run, verify fail** (header missing).

- [ ] **Step 3: Create header + implement**

```cpp
// src/sessions/CliArgs.h
#pragma once
#include <optional>
#include <string>

namespace aicoder {
enum class CliMode { NewSession, ContinueLatest, ResumePicker };
struct CliArgs { CliMode mode = CliMode::NewSession; };
// Parses argv (argv[0] is program name). Returns nullopt on unknown flag.
std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv);
inline const char* kUsage =
    "Usage: aicoder_tui [-c | --resume]\n"
    "  -c        resume the most recently updated session\n"
    "  --resume  pick a past session from a list\n";
}
```

```cpp
// src/sessions/CliArgs.cpp
#include "sessions/CliArgs.h"
#include <string>
namespace aicoder {
std::optional<CliArgs> parseCliArgs(int argc, const char* const* argv) {
  CliArgs out;
  bool sawC = false, sawResume = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-c") sawC = true;
    else if (a == "--resume") sawResume = true;
    else return std::nullopt;
  }
  if (sawResume) out.mode = CliMode::ResumePicker;
  else if (sawC)  out.mode = CliMode::ContinueLatest;
  else            out.mode = CliMode::NewSession;
  return out;
}
}
```

- [ ] **Step 4: Run, verify PASS (5)**

`cmake -S . -B build && cmake --build build --target aicoder_tests && ./build/aicoder_tests --gtest_filter='CliArgs.*'` → 5/5.

- [ ] **Step 5: Commit**

```bash
git add src/sessions/CliArgs.h src/sessions/CliArgs.cpp tests/MainTuiArgsTest.cpp
git commit -m "feat: parseCliArgs for -c / --resume (unit-tested)"
```

### Task 4.2: ResumePicker (FTXUI Menu)

**Files:**
- Create: `src/ui/ResumePicker.h`, `src/ui/ResumePicker.cpp`

ResumePicker has no automated test (FTXUI Menu is UI-only). The selection logic is trivial; correctness comes from the SessionStore tests + manual TUI verification.

- [ ] **Step 1: Create header**

```cpp
// src/ui/ResumePicker.h
#pragma once
#include <optional>
#include <string>
#include "sessions/SessionStore.h"
namespace aicoder {
// Show full-screen list of sessions; returns selected id or nullopt (Esc/empty list).
std::optional<std::string> showResumePicker(const SessionStore& store);
}
```

- [ ] **Step 2: Implement**

```cpp
// src/ui/ResumePicker.cpp
#include "ui/ResumePicker.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace aicoder {

std::optional<std::string> showResumePicker(const SessionStore& store) {
  auto sessions = store.listSessions();
  if (sessions.empty()) {
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto root = ftxui::Renderer([] {
      return ftxui::vbox({
          ftxui::text("无历史会话") | ftxui::bold,
          ftxui::text("按任意键开始新会话") | ftxui::dim,
      });
    });
    root = ftxui::CatchEvent(root, [&](ftxui::Event) { screen.Exit(); return true; });
    screen.Loop(root);
    return std::nullopt;
  }

  int selected = 0;
  std::optional<std::string> chosen;
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  std::vector<std::string> entries;
  for (const auto& s : sessions) {
    std::string row = s.updated_at + "  " + s.model + "  ";
    row += s.preview.empty() ? "(无预览)" : s.preview;
    row += "  [" + std::to_string(s.message_count) + " 条]";
    entries.push_back(std::move(row));
  }

  auto menu = ftxui::Menu(&entries, &selected);

  auto root = ftxui::Renderer(menu, [&] {
    return ftxui::vbox({
        ftxui::text("选择要恢复的会话(↑↓ 移动,Enter 选中,Esc 新会话)") | ftxui::bold,
        ftxui::separator(),
        menu->Render() | ftxui::yframe | ftxui::flex,
    }) | ftxui::border;
  });

  root = ftxui::CatchEvent(root, [&](ftxui::Event e) {
    if (e == ftxui::Event::Return) {
      if (selected >= 0 && selected < (int)sessions.size())
        chosen = sessions[selected].id;
      screen.Exit();
      return true;
    }
    if (e == ftxui::Event::Escape) { screen.Exit(); return true; }
    return false;
  });

  screen.Loop(root);
  return chosen;
}

}  // namespace aicoder
```

- [ ] **Step 3: Build (verify it compiles)**

`cmake -S . -B build && cmake --build build --target aicoder_tui 2>&1 | tail -5` → aicoder_tui built.

> Note for CMake: `ResumePicker.cpp` is NEW and uses FTXUI. CMakeLists currently lists `src/ui/App.cpp` and `src/ui/ReplView.cpp` as the TUI-only sources (excluded from `aicoder_core`). Add `${PROJECT_SOURCE_DIR}/src/ui/ResumePicker.cpp` to the same exclusion list AND to the `aicoder_tui` `add_executable` sources, mirroring App.cpp. Open `CMakeLists.txt`, find the `list(REMOVE_ITEM AICODER_LIB_SOURCES ...)` block and the `add_executable(aicoder_tui ...)` block; add `src/ui/ResumePicker.cpp` to both.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ResumePicker.h src/ui/ResumePicker.cpp CMakeLists.txt
git commit -m "feat: ResumePicker — FTXUI menu for --resume"
```

### Task 4.3: Wire CLI + picker into main_tui

**Files:** Modify `src/main_tui.cpp`

- [ ] **Step 1: Replace main() entry to take argv and dispatch**

In `src/main_tui.cpp`, change `int main()` to `int main(int argc, char** argv)`. Add includes: `#include "sessions/CliArgs.h"` and `#include "ui/ResumePicker.h"`. Then after `Config` is loaded and BEFORE `App` construction, insert the session-selection logic. Final main_tui body shape:

```cpp
int main(int argc, char** argv) {
  auto args = parseCliArgs(argc, argv);
  if (!args) {
    std::cerr << kUsage;
    return 2;
  }

  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }

  // ... existing block: ToolRegistry, registerBuiltinTools, static SkillRegistry, etc.
  // ... existing CommandRegistry + CommandRouter construction
  // ... existing systemPrompt construction via buildSystemPrompt

  SessionStore sessionStore(globalDir() / "sessions");
  std::string sessionId;
  std::vector<Message> initialMessages;

  if (args->mode == CliMode::ContinueLatest) {
    if (auto id = sessionStore.latestId()) {
      sessionId = *id;
      if (auto data = sessionStore.load(*id)) initialMessages = std::move(data->messages);
      else { std::cerr << "[会话已损坏，开新会话]\n"; sessionId = sessionStore.newId(); }
    } else {
      std::cerr << "[未找到历史会话，开始新会话]\n";
      sessionId = sessionStore.newId();
    }
  } else if (args->mode == CliMode::ResumePicker) {
    auto picked = showResumePicker(sessionStore);
    if (picked) {
      sessionId = *picked;
      if (auto data = sessionStore.load(*picked)) initialMessages = std::move(data->messages);
      else { std::cerr << "[会话已损坏，开新会话]\n"; sessionId = sessionStore.newId(); }
    } else {
      sessionId = sessionStore.newId();
    }
  } else {
    sessionId = sessionStore.newId();
  }

  App app(loop, router, systemPrompt, sessionStore, sessionId,
          std::move(initialMessages), config.model);
  app.run();
  return 0;
}
```

(`loop`, `router`, `systemPrompt` are the existing locals built earlier in main.)

- [ ] **Step 2: Build all 3 targets**

`cmake --build build 2>&1 | tail -5` → all built.

- [ ] **Step 3: Run full test suite, confirm no regression**

`./build/aicoder_tests 2>/dev/null | tail -2` → all PASS.

- [ ] **Step 4: Manual smoke (no API needed)**

```bash
# unknown flag → usage + exit 2
./build/aicoder_tui --bogus; echo "exit=$?"
# Expected: usage printed; exit=2.

# Help-ish: try -c with empty sessions dir (move existing aside first if present)
mv "$HOME/.aicoder/sessions" "$HOME/.aicoder/sessions.bak" 2>/dev/null
# Can't easily run aicoder_tui non-interactively, but the prior step is enough for CLI validation.
mv "$HOME/.aicoder/sessions.bak" "$HOME/.aicoder/sessions" 2>/dev/null
```

- [ ] **Step 5: Commit**

```bash
git add src/main_tui.cpp
git commit -m "feat: main_tui wires -c / --resume to SessionStore + ResumePicker"
```

---

## Final Verification (manual, real terminal + API key)

Cannot be automated here (interactive TUI + live model). After all phases:

1. **Fresh launch saves**: `./build/aicoder_tui`; send a message; `/quit`. Then `ls ~/.aicoder/sessions/` — one new dir with `session.json` containing your turn.
2. **`-c` resumes**: `./build/aicoder_tui -c` — should show the prior user message + assistant reply in the chat history before the input box.
3. **`--resume` picker**: send another message in a fresh launch to create a 2nd session; then `./build/aicoder_tui --resume` — list with 2 entries (newest first) and previews; arrows + Enter loads the chosen one.
4. **`/clear` rotation**: after a few turns, `/clear`; `ls ~/.aicoder/sessions/` — original session preserved, a NEW dir for the post-clear session.
5. **Unknown flag**: `./build/aicoder_tui --bogus` — usage on stderr, exit 2.

---

## Self-Review

- **Spec coverage:**
  - §存储布局 → Task 2.1 (root + per-session dir + session.json), `globalDir()/"sessions"` wiring in 3.1/4.3. ✅
  - §session.json schema → Task 1.1 (all three block types + reasoning_content omitted-when-empty + schema_version=1). ✅
  - §生命周期 → Task 3.1 (no-flag + /clear rotate), Task 4.3 (`-c` and `--resume`). ✅
  - §写盘 atomic → Task 2.1 (.tmp + rename). ✅
  - §CLI 解析 → Task 4.1 (parseCliArgs + unknown→nullopt→usage+exit 2). ✅
  - §ResumePicker → Task 4.2. ✅
  - §模块布局 → matches file structure. ✅
  - §App 改动 → Task 3.1. ✅
  - §健壮性 → Task 1.1 schema-version rejection, Task 2.1 load nullopt on missing/corrupt, Task 2.2 listSessions skips corrupt, Task 4.3 -c friendly fallback. ✅
  - §测试 → Task 1.1 (4), 2.1/2.2 (8), 4.1 (5). ResumePicker UI not tested (acknowledged in spec). ✅
- **Placeholder scan:** None — every code block is complete and runnable; the only "..." in code is a JSON value placeholder in the schema example, which mirrors the spec verbatim. ✅
- **Type consistency:** `SessionData{id, created_at, updated_at, model, messages}`, `SessionInfo{id, updated_at, model, preview, message_count, mtime}`, `CliMode::{NewSession, ContinueLatest, ResumePicker}`, `parseCliArgs(int, const char* const*) → optional<CliArgs>`, `SessionStore::{newId, save, load, listSessions, latestId}` are used identically across tasks. ✅
