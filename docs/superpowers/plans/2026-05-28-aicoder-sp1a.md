# AICoder SP1a 实现计划 — 控制台 Agentic 内核

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 C++20 实现一个纯控制台、单线程的 agentic 编码助手内核：能与 OpenAI 兼容模型多轮对话，自动发起结构化工具调用（读文件 / 列目录），执行后回灌结果并重入循环，直到模型不再要工具。

**Architecture:** `AgentLoop` 只依赖抽象 `LlmClient`，不知道背后是 OpenAI 还是 Claude、是控制台还是 TUI。`DefaultLlmClient` 组合 `Provider`（canonical Message ⇄ OpenAI JSON 编解码）+ `Transport`（libcurl POST）。工具失败回灌给模型，基建失败中断本轮。所有跨边界类型都有抽象接口，便于注入 fake 做单元测试。

**Tech Stack:** C++20 · CMake 3.16+ · libcurl 8.5 · nlohmann/json（系统头，vendored 兜底）· GoogleTest。

**Spec:** `AICoder/docs/superpowers/specs/2026-05-28-aicoder-sp1-design.md`

**工作目录:** 所有路径相对 `~/lib/CppCode/AICoder/`。在该目录下执行命令。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | 构建：`aicoder_core` 静态库 + `aicoder` 可执行 + `aicoder_tests` 测试 |
| `src/core/Json.h` | `using json = nlohmann::json` 别名 |
| `src/core/Errors.h` | `ToolError` / `LlmError` / `ConfigError` 异常 |
| `src/core/Version.h` `.cpp` | 版本字符串（让核心库非空 + 烟测目标） |
| `src/core/Message.h` | `Role` / `ContentBlock`(variant) / `Message` + helper |
| `src/core/Tool.h` | `Tool`（含 execute 回调）/ `ToolSpec`（无回调，给 Provider 编码用） |
| `src/core/ToolRegistry.h` `.cpp` | 注册 / `specs()` / `invoke()`（捕获 ToolError → 错误 result） |
| `src/core/AgentLoop.h` `.cpp` | ⭐ 迭代循环 |
| `src/llm/Response.h` | `Response{assistant_message, finish_reason}` |
| `src/llm/LlmClient.h` | 抽象：`Response send(messages, tools)` |
| `src/llm/Provider.h` | 抽象：`encodeRequest` / `decodeResponse` |
| `src/llm/OpenAIProvider.h` `.cpp` | OpenAI 编解码（tool_calls） |
| `src/llm/Transport.h` | 抽象 `Transport` + `HttpResponse` |
| `src/llm/HttpTransport.h` `.cpp` | libcurl POST 实现 |
| `src/llm/DefaultLlmClient.h` `.cpp` | 编排 Provider + Transport |
| `src/tools/ReadFileTool.h` `.cpp` | `makeReadFileTool()` |
| `src/tools/ListDirTool.h` `.cpp` | `makeListDirTool()` |
| `src/config/Config.h` `.cpp` | `Config::fromEnv()` fail-fast |
| `src/commands/CommandRouter.h` `.cpp` | `/quit` `/clear` 拦截 |
| `src/ui/ConsoleRepl.h` `.cpp` | cin/cout REPL（注入 istream/ostream 以可测） |
| `src/main.cpp` | 装配并启动 |
| `tests/*.cpp` | GoogleTest（glob 自动发现） |

---

## Task 1: 项目骨架与构建系统

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/core/Json.h`, `src/core/Version.h`, `src/core/Version.cpp`
- Create: `src/main.cpp`
- Create: `tests/SmokeTest.cpp`

- [ ] **Step 1: 写 `src/core/Json.h`**

```cpp
#pragma once
#include <nlohmann/json.hpp>

namespace aicoder {
using json = nlohmann::json;
}
```

- [ ] **Step 2: 写 `src/core/Version.h` 与 `Version.cpp`**

`src/core/Version.h`:
```cpp
#pragma once
namespace aicoder {
const char* version();
}
```

`src/core/Version.cpp`:
```cpp
#include "core/Version.h"
namespace aicoder {
const char* version() { return "0.1.0-sp1a"; }
}
```

- [ ] **Step 3: 写 `src/main.cpp`（先放占位，Task 13 替换）**

```cpp
#include <iostream>
#include "core/Version.h"

int main() {
  std::cout << "AICoder " << aicoder::version() << "\n";
  return 0;
}
```

- [ ] **Step 4: 写 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(AICoder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)
enable_testing()

# nlohmann/json: 系统头优先，vendored 兜底
find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp PATHS /usr/include /usr/local/include)
if(NOT NLOHMANN_JSON_INCLUDE)
  set(NLOHMANN_JSON_INCLUDE "${PROJECT_SOURCE_DIR}/third_party")
endif()
message(STATUS "nlohmann/json include: ${NLOHMANN_JSON_INCLUDE}")

# 核心库 = src 下除 main.cpp 外的所有 .cpp（CONFIGURE_DEPENDS 让新增文件自动纳入）
file(GLOB_RECURSE AICODER_LIB_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")

add_library(aicoder_core STATIC ${AICODER_LIB_SOURCES})
target_include_directories(aicoder_core PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${NLOHMANN_JSON_INCLUDE}")
target_link_libraries(aicoder_core PUBLIC CURL::libcurl Threads::Threads)

add_executable(aicoder "${PROJECT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(aicoder PRIVATE aicoder_core)

file(GLOB_RECURSE AICODER_TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp")
add_executable(aicoder_tests ${AICODER_TEST_SOURCES})
target_link_libraries(aicoder_tests PRIVATE aicoder_core GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(aicoder_tests)
```

- [ ] **Step 5: 写烟测 `tests/SmokeTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <string>
#include "core/Json.h"
#include "core/Version.h"

TEST(Smoke, JsonParses) {
  auto j = aicoder::json::parse(R"({"a":1})");
  EXPECT_EQ(j["a"].get<int>(), 1);
}

TEST(Smoke, VersionNonEmpty) {
  EXPECT_FALSE(std::string(aicoder::version()).empty());
}
```

- [ ] **Step 6: 配置并构建**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: 配置成功（打印 nlohmann/json include 路径），编译出 `build/aicoder` 与 `build/aicoder_tests`，无错误。

- [ ] **Step 7: 跑烟测**

Run: `./build/aicoder_tests --gtest_filter='Smoke.*'`
Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 8: 跑可执行**

Run: `./build/aicoder`
Expected: 打印 `AICoder 0.1.0-sp1a`

- [ ] **Step 9: 提交**

```bash
git init    # 若尚未是 git 仓库
printf 'build/\n' > .gitignore
git add CMakeLists.txt .gitignore src/ tests/ docs/
git commit -m "chore: scaffold AICoder SP1a build system + smoke test"
```

---

## Task 2: Message 模型与异常

**Files:**
- Create: `src/core/Errors.h`, `src/core/Message.h`
- Test: `tests/MessageTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/MessageTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "core/Message.h"

using namespace aicoder;

TEST(Message, UserTextHelperBuildsUserRoleWithTextBlock) {
  Message m = userText("hello");
  EXPECT_EQ(m.role, Role::User);
  ASSERT_EQ(m.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&m.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "hello");
}

TEST(Message, ToolUseBlockHoldsNameAndInput) {
  ContentBlock b = ToolUseBlock{"id1", "read_file", json{{"path", "x"}}};
  auto* tu = std::get_if<ToolUseBlock>(&b);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "x");
}

TEST(Message, ToolResultDefaultsToNotError) {
  ToolResultBlock r{"id1", "content"};
  EXPECT_FALSE(r.is_error);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: 编译失败，`core/Message.h` 不存在 / `userText` 未声明。

- [ ] **Step 3: 写 `src/core/Errors.h`**

```cpp
#pragma once
#include <stdexcept>

namespace aicoder {
struct ToolError   : std::runtime_error { using std::runtime_error::runtime_error; };
struct LlmError    : std::runtime_error { using std::runtime_error::runtime_error; };
struct ConfigError : std::runtime_error { using std::runtime_error::runtime_error; };
}
```

- [ ] **Step 4: 写 `src/core/Message.h`**

```cpp
#pragma once
#include <string>
#include <variant>
#include <vector>
#include "core/Json.h"

namespace aicoder {

enum class Role { System, User, Assistant, Tool };

struct TextBlock       { std::string text; };
struct ToolUseBlock    { std::string id; std::string name; json input; };
struct ToolResultBlock { std::string tool_use_id; std::string content; bool is_error = false; };

using ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock>;

struct Message {
  Role role;
  std::vector<ContentBlock> content;
};

inline Message userText(std::string t) {
  return Message{Role::User, {TextBlock{std::move(t)}}};
}
inline Message systemText(std::string t) {
  return Message{Role::System, {TextBlock{std::move(t)}}};
}

// 把一条消息里所有 TextBlock 拼起来
inline std::string assistantText(const Message& m) {
  std::string out;
  for (const auto& b : m.content)
    if (const auto* tb = std::get_if<TextBlock>(&b)) out += tb->text;
  return out;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='Message.*'`
Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/core/Errors.h src/core/Message.h tests/MessageTest.cpp
git commit -m "feat: canonical Message model + error types"
```

---

## Task 3: Tool / ToolSpec / ToolRegistry

**Files:**
- Create: `src/core/Tool.h`, `src/core/ToolRegistry.h`, `src/core/ToolRegistry.cpp`
- Test: `tests/ToolRegistryTest.cpp`

- [ ] **Step 1: 写 `src/core/Tool.h`**

```cpp
#pragma once
#include <functional>
#include <string>
#include "core/Json.h"

namespace aicoder {

// 完整工具：带执行回调。execute 成功返回结果文本；失败抛 ToolError。
struct Tool {
  std::string name;
  std::string description;
  json input_schema;
  std::function<std::string(const json&)> execute;
};

// 工具规格：不含回调，提供给 Provider 编码进请求的 tools 字段。
struct ToolSpec {
  std::string name;
  std::string description;
  json input_schema;
};

}
```

- [ ] **Step 2: 写失败测试 `tests/ToolRegistryTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "core/ToolRegistry.h"
#include "core/Errors.h"

using namespace aicoder;

static Tool okTool() {
  Tool t;
  t.name = "echo";
  t.description = "返回输入的 text";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json& in) { return in.value("text", std::string("")); };
  return t;
}

static Tool boomTool() {
  Tool t;
  t.name = "boom";
  t.description = "总是失败";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json&) -> std::string { throw ToolError("炸了"); };
  return t;
}

TEST(ToolRegistry, RegisterAndHas) {
  ToolRegistry r;
  r.registerTool(okTool());
  EXPECT_TRUE(r.has("echo"));
  EXPECT_FALSE(r.has("nope"));
}

TEST(ToolRegistry, SpecsListsRegisteredTools) {
  ToolRegistry r;
  r.registerTool(okTool());
  auto specs = r.specs();
  ASSERT_EQ(specs.size(), 1u);
  EXPECT_EQ(specs[0].name, "echo");
}

TEST(ToolRegistry, InvokeSuccessReturnsResult) {
  ToolRegistry r;
  r.registerTool(okTool());
  ToolResultBlock res = r.invoke("u1", "echo", json{{"text", "hi"}});
  EXPECT_EQ(res.tool_use_id, "u1");
  EXPECT_EQ(res.content, "hi");
  EXPECT_FALSE(res.is_error);
}

TEST(ToolRegistry, InvokeUnknownToolReturnsErrorResult) {
  ToolRegistry r;
  ToolResultBlock res = r.invoke("u1", "ghost", json::object());
  EXPECT_TRUE(res.is_error);
  EXPECT_NE(res.content.find("ghost"), std::string::npos);
}

TEST(ToolRegistry, InvokeToolErrorIsCaughtAndWrapped) {
  ToolRegistry r;
  r.registerTool(boomTool());
  ToolResultBlock res = r.invoke("u1", "boom", json::object());
  EXPECT_TRUE(res.is_error);
  EXPECT_NE(res.content.find("炸了"), std::string::npos);
}
```

- [ ] **Step 3: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `core/ToolRegistry.h` 不存在。

- [ ] **Step 4: 写 `src/core/ToolRegistry.h`**

```cpp
#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/Tool.h"
#include "core/Message.h"

namespace aicoder {

class ToolRegistry {
public:
  void registerTool(Tool tool);
  bool has(const std::string& name) const;
  std::vector<ToolSpec> specs() const;
  // 执行工具；捕获 ToolError / 未知工具 → 返回 is_error=true 的 result。
  ToolResultBlock invoke(const std::string& toolUseId,
                         const std::string& name,
                         const json& input) const;
private:
  std::map<std::string, Tool> tools_;
};

}
```

- [ ] **Step 5: 写 `src/core/ToolRegistry.cpp`**

```cpp
#include "core/ToolRegistry.h"
#include "core/Errors.h"

namespace aicoder {

void ToolRegistry::registerTool(Tool tool) {
  tools_[tool.name] = std::move(tool);
}

bool ToolRegistry::has(const std::string& name) const {
  return tools_.find(name) != tools_.end();
}

std::vector<ToolSpec> ToolRegistry::specs() const {
  std::vector<ToolSpec> out;
  for (const auto& [name, t] : tools_)
    out.push_back(ToolSpec{t.name, t.description, t.input_schema});
  return out;
}

ToolResultBlock ToolRegistry::invoke(const std::string& toolUseId,
                                     const std::string& name,
                                     const json& input) const {
  auto it = tools_.find(name);
  if (it == tools_.end())
    return ToolResultBlock{toolUseId, "未知工具: " + name, true};
  try {
    std::string out = it->second.execute(input);
    return ToolResultBlock{toolUseId, out, false};
  } catch (const ToolError& e) {
    return ToolResultBlock{toolUseId, std::string("工具执行失败: ") + e.what(), true};
  }
}

}
```

- [ ] **Step 6: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='ToolRegistry.*'`
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 7: 提交**

```bash
git add src/core/Tool.h src/core/ToolRegistry.h src/core/ToolRegistry.cpp tests/ToolRegistryTest.cpp
git commit -m "feat: Tool/ToolSpec + ToolRegistry with error-wrapping invoke"
```

---

## Task 4: ReadFileTool

**Files:**
- Create: `src/tools/ReadFileTool.h`, `src/tools/ReadFileTool.cpp`
- Test: `tests/ReadFileToolTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/ReadFileToolTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include "tools/ReadFileTool.h"
#include "core/Errors.h"

using namespace aicoder;

TEST(ReadFileTool, ReadsExistingFile) {
  std::string path = std::string(std::tmpnam(nullptr));
  { std::ofstream(path) << "hello world"; }
  Tool t = makeReadFileTool();
  std::string out = t.execute(json{{"path", path}});
  EXPECT_EQ(out, "hello world");
  std::remove(path.c_str());
}

TEST(ReadFileTool, MissingFileThrowsToolError) {
  Tool t = makeReadFileTool();
  EXPECT_THROW(t.execute(json{{"path", "/no/such/file/xyz"}}), ToolError);
}

TEST(ReadFileTool, MissingPathArgThrowsToolError) {
  Tool t = makeReadFileTool();
  EXPECT_THROW(t.execute(json::object()), ToolError);
}

TEST(ReadFileTool, HasNameAndSchema) {
  Tool t = makeReadFileTool();
  EXPECT_EQ(t.name, "read_file");
  EXPECT_EQ(t.input_schema["type"].get<std::string>(), "object");
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `tools/ReadFileTool.h` 不存在。

- [ ] **Step 3: 写 `src/tools/ReadFileTool.h`**

```cpp
#pragma once
#include "core/Tool.h"

namespace aicoder {
Tool makeReadFileTool();
}
```

- [ ] **Step 4: 写 `src/tools/ReadFileTool.cpp`**

```cpp
#include "tools/ReadFileTool.h"
#include "core/Errors.h"
#include <fstream>
#include <sstream>

namespace aicoder {

Tool makeReadFileTool() {
  Tool t;
  t.name = "read_file";
  t.description = "读取指定路径文件的全部文本内容";
  t.input_schema = json{
    {"type", "object"},
    {"properties", {{"path", {{"type", "string"}, {"description", "要读取的文件路径"}}}}},
    {"required", json::array({"path"})}
  };
  t.execute = [](const json& input) -> std::string {
    if (!input.contains("path") || !input["path"].is_string())
      throw ToolError("缺少字符串参数 path");
    std::string path = input["path"].get<std::string>();
    std::ifstream f(path, std::ios::binary);
    if (!f) throw ToolError("无法打开文件: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  return t;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='ReadFileTool.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/tools/ReadFileTool.h src/tools/ReadFileTool.cpp tests/ReadFileToolTest.cpp
git commit -m "feat: read_file tool"
```

---

## Task 5: ListDirTool

**Files:**
- Create: `src/tools/ListDirTool.h`, `src/tools/ListDirTool.cpp`
- Test: `tests/ListDirToolTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/ListDirToolTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/ListDirTool.h"
#include "core/Errors.h"

using namespace aicoder;
namespace fs = std::filesystem;

TEST(ListDirTool, ListsEntries) {
  fs::path dir = fs::temp_directory_path() / "aicoder_listdir_test";
  fs::create_directories(dir / "sub");
  { std::ofstream(dir / "a.txt") << "x"; }
  Tool t = makeListDirTool();
  std::string out = t.execute(json{{"path", dir.string()}});
  EXPECT_NE(out.find("a.txt"), std::string::npos);
  EXPECT_NE(out.find("sub"), std::string::npos);
  fs::remove_all(dir);
}

TEST(ListDirTool, NonDirThrowsToolError) {
  Tool t = makeListDirTool();
  EXPECT_THROW(t.execute(json{{"path", "/no/such/dir/xyz"}}), ToolError);
}

TEST(ListDirTool, HasNameAndSchema) {
  Tool t = makeListDirTool();
  EXPECT_EQ(t.name, "list_dir");
  EXPECT_EQ(t.input_schema["type"].get<std::string>(), "object");
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `tools/ListDirTool.h` 不存在。

- [ ] **Step 3: 写 `src/tools/ListDirTool.h`**

```cpp
#pragma once
#include "core/Tool.h"

namespace aicoder {
Tool makeListDirTool();
}
```

- [ ] **Step 4: 写 `src/tools/ListDirTool.cpp`**

```cpp
#include "tools/ListDirTool.h"
#include "core/Errors.h"
#include <filesystem>

namespace aicoder {

Tool makeListDirTool() {
  Tool t;
  t.name = "list_dir";
  t.description = "列出指定目录下的条目（文件 / 子目录）";
  t.input_schema = json{
    {"type", "object"},
    {"properties", {{"path", {{"type", "string"}, {"description", "要列出的目录路径"}}}}},
    {"required", json::array({"path"})}
  };
  t.execute = [](const json& input) -> std::string {
    namespace fs = std::filesystem;
    if (!input.contains("path") || !input["path"].is_string())
      throw ToolError("缺少字符串参数 path");
    std::string path = input["path"].get<std::string>();
    std::error_code ec;
    if (!fs::is_directory(path, ec))
      throw ToolError("不是目录或不存在: " + path);
    std::string out;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
      out += entry.is_directory() ? "[D] " : "[F] ";
      out += entry.path().filename().string();
      out += "\n";
    }
    return out.empty() ? "(空目录)" : out;
  };
  return t;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='ListDirTool.*'`
Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/tools/ListDirTool.h src/tools/ListDirTool.cpp tests/ListDirToolTest.cpp
git commit -m "feat: list_dir tool"
```

---

## Task 6: LLM 抽象接口（Response / LlmClient / Provider / Transport）

只新增头文件（接口契约），无 .cpp，无独立测试——下游任务的测试覆盖它们。

**Files:**
- Create: `src/llm/Response.h`, `src/llm/LlmClient.h`, `src/llm/Provider.h`, `src/llm/Transport.h`

- [ ] **Step 1: 写 `src/llm/Response.h`**

```cpp
#pragma once
#include <string>
#include "core/Message.h"

namespace aicoder {
struct Response {
  Message assistant_message;   // role=Assistant，含 TextBlock 和/或 ToolUseBlock
  std::string finish_reason;   // "stop" / "tool_calls" / ...
};
}
```

- [ ] **Step 2: 写 `src/llm/LlmClient.h`**

```cpp
#pragma once
#include <vector>
#include "core/Message.h"
#include "core/Tool.h"
#include "llm/Response.h"

namespace aicoder {
class LlmClient {
public:
  virtual ~LlmClient() = default;
  // 基建失败（网络/HTTP/解析）抛 LlmError，不返回。
  virtual Response send(const std::vector<Message>& messages,
                        const std::vector<ToolSpec>& tools) = 0;
};
}
```

- [ ] **Step 3: 写 `src/llm/Provider.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"
#include "core/Tool.h"
#include "llm/Response.h"

namespace aicoder {
class Provider {
public:
  virtual ~Provider() = default;
  virtual json encodeRequest(const std::vector<Message>& messages,
                             const std::vector<ToolSpec>& tools,
                             const std::string& model) const = 0;
  virtual Response decodeResponse(const json& body) const = 0;
};
}
```

- [ ] **Step 4: 写 `src/llm/Transport.h`**

```cpp
#pragma once
#include <string>
#include <vector>

namespace aicoder {
struct HttpResponse {
  long status = 0;
  std::string body;
};
class Transport {
public:
  virtual ~Transport() = default;
  // 传输层失败（连接/超时/URL 错误）抛 LlmError；HTTP 非 2xx 仍正常返回（由上层判断）。
  virtual HttpResponse post(const std::string& url,
                            const std::string& body,
                            const std::vector<std::string>& headers) = 0;
};
}
```

- [ ] **Step 5: 确认仍可构建**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: 构建成功（新头文件未被引用，但语法正确）。

- [ ] **Step 6: 提交**

```bash
git add src/llm/Response.h src/llm/LlmClient.h src/llm/Provider.h src/llm/Transport.h
git commit -m "feat: LLM abstraction interfaces (Response/LlmClient/Provider/Transport)"
```

---

## Task 7: OpenAIProvider（编解码）

**Files:**
- Create: `src/llm/OpenAIProvider.h`, `src/llm/OpenAIProvider.cpp`
- Test: `tests/OpenAIProviderTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/OpenAIProviderTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "llm/OpenAIProvider.h"

using namespace aicoder;

TEST(OpenAIProvider, EncodesSystemUserAndTools) {
  OpenAIProvider p;
  std::vector<Message> msgs = {systemText("sys"), userText("hi")};
  std::vector<ToolSpec> tools = {{"read_file", "读文件", json{{"type", "object"}}}};
  json req = p.encodeRequest(msgs, tools, "deepseek-chat");

  EXPECT_EQ(req["model"].get<std::string>(), "deepseek-chat");
  ASSERT_EQ(req["messages"].size(), 2u);
  EXPECT_EQ(req["messages"][0]["role"].get<std::string>(), "system");
  EXPECT_EQ(req["messages"][1]["role"].get<std::string>(), "user");
  ASSERT_TRUE(req.contains("tools"));
  EXPECT_EQ(req["tools"][0]["function"]["name"].get<std::string>(), "read_file");
  EXPECT_EQ(req["tool_choice"].get<std::string>(), "auto");
}

TEST(OpenAIProvider, EncodesAssistantToolCallAndToolResult) {
  OpenAIProvider p;
  Message assistant{Role::Assistant, {ToolUseBlock{"call_1", "read_file", json{{"path", "x"}}}}};
  Message toolMsg{Role::Tool, {ToolResultBlock{"call_1", "file contents", false}}};
  json req = p.encodeRequest({assistant, toolMsg}, {}, "m");

  ASSERT_EQ(req["messages"].size(), 2u);
  auto& a = req["messages"][0];
  EXPECT_EQ(a["role"].get<std::string>(), "assistant");
  EXPECT_EQ(a["tool_calls"][0]["id"].get<std::string>(), "call_1");
  EXPECT_EQ(a["tool_calls"][0]["function"]["name"].get<std::string>(), "read_file");
  auto& tr = req["messages"][1];
  EXPECT_EQ(tr["role"].get<std::string>(), "tool");
  EXPECT_EQ(tr["tool_call_id"].get<std::string>(), "call_1");
  EXPECT_EQ(tr["content"].get<std::string>(), "file contents");
}

TEST(OpenAIProvider, DecodesPlainTextResponse) {
  OpenAIProvider p;
  json body = {
    {"choices", {{
      {"finish_reason", "stop"},
      {"message", {{"role", "assistant"}, {"content", "你好"}}}
    }}}
  };
  Response r = p.decodeResponse(body);
  EXPECT_EQ(r.finish_reason, "stop");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "你好");
}

TEST(OpenAIProvider, DecodesToolCallResponse) {
  OpenAIProvider p;
  json body = {
    {"choices", {{
      {"finish_reason", "tool_calls"},
      {"message", {
        {"role", "assistant"},
        {"content", nullptr},
        {"tool_calls", {{
          {"id", "call_9"},
          {"type", "function"},
          {"function", {{"name", "read_file"}, {"arguments", "{\"path\":\"a.txt\"}"}}}
        }}}
      }}
    }}}
  };
  Response r = p.decodeResponse(body);
  EXPECT_EQ(r.finish_reason, "tool_calls");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tu = std::get_if<ToolUseBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->id, "call_9");
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "a.txt");
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `llm/OpenAIProvider.h` 不存在。

- [ ] **Step 3: 写 `src/llm/OpenAIProvider.h`**

```cpp
#pragma once
#include "llm/Provider.h"

namespace aicoder {
class OpenAIProvider : public Provider {
public:
  json encodeRequest(const std::vector<Message>& messages,
                     const std::vector<ToolSpec>& tools,
                     const std::string& model) const override;
  Response decodeResponse(const json& body) const override;
};
}
```

- [ ] **Step 4: 写 `src/llm/OpenAIProvider.cpp`**

```cpp
#include "llm/OpenAIProvider.h"

namespace aicoder {

static const char* roleStr(Role r) {
  switch (r) {
    case Role::System: return "system";
    case Role::User: return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool: return "tool";
  }
  return "user";
}

json OpenAIProvider::encodeRequest(const std::vector<Message>& messages,
                                   const std::vector<ToolSpec>& tools,
                                   const std::string& model) const {
  json j;
  j["model"] = model;
  j["messages"] = json::array();

  for (const auto& msg : messages) {
    if (msg.role == Role::Tool) {
      // 每个 ToolResultBlock 展开成一条 role=tool 消息
      for (const auto& b : msg.content) {
        if (const auto* tr = std::get_if<ToolResultBlock>(&b)) {
          j["messages"].push_back({
            {"role", "tool"},
            {"tool_call_id", tr->tool_use_id},
            {"content", tr->content}
          });
        }
      }
      continue;
    }
    if (msg.role == Role::Assistant) {
      std::string text;
      json toolCalls = json::array();
      for (const auto& b : msg.content) {
        if (const auto* tb = std::get_if<TextBlock>(&b)) {
          text += tb->text;
        } else if (const auto* tu = std::get_if<ToolUseBlock>(&b)) {
          toolCalls.push_back({
            {"id", tu->id},
            {"type", "function"},
            {"function", {{"name", tu->name}, {"arguments", tu->input.dump()}}}
          });
        }
      }
      json m{{"role", "assistant"}};
      if (!text.empty()) m["content"] = text; else m["content"] = nullptr;
      if (!toolCalls.empty()) m["tool_calls"] = toolCalls;
      j["messages"].push_back(m);
      continue;
    }
    // System / User：拼接所有 TextBlock
    std::string text;
    for (const auto& b : msg.content)
      if (const auto* tb = std::get_if<TextBlock>(&b)) text += tb->text;
    j["messages"].push_back({{"role", roleStr(msg.role)}, {"content", text}});
  }

  if (!tools.empty()) {
    j["tools"] = json::array();
    for (const auto& t : tools) {
      j["tools"].push_back({
        {"type", "function"},
        {"function", {
          {"name", t.name},
          {"description", t.description},
          {"parameters", t.input_schema}
        }}
      });
    }
    j["tool_choice"] = "auto";
  }
  return j;
}

Response OpenAIProvider::decodeResponse(const json& body) const {
  const auto& choice = body.at("choices").at(0);
  Response r;
  r.finish_reason = choice.value("finish_reason", std::string());

  const auto& m = choice.at("message");
  Message am;
  am.role = Role::Assistant;

  if (m.contains("content") && !m["content"].is_null()) {
    std::string c = m["content"].get<std::string>();
    if (!c.empty()) am.content.push_back(TextBlock{c});
  }
  if (m.contains("tool_calls") && m["tool_calls"].is_array()) {
    for (const auto& tc : m["tool_calls"]) {
      ToolUseBlock tu;
      tu.id = tc.value("id", std::string());
      tu.name = tc.at("function").at("name").get<std::string>();
      std::string args = tc.at("function").value("arguments", std::string("{}"));
      tu.input = args.empty() ? json::object() : json::parse(args);
      am.content.push_back(tu);
    }
  }
  r.assistant_message = am;
  return r;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='OpenAIProvider.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/llm/OpenAIProvider.h src/llm/OpenAIProvider.cpp tests/OpenAIProviderTest.cpp
git commit -m "feat: OpenAIProvider encode/decode with tool_calls"
```

---

## Task 8: HttpTransport（libcurl）

**Files:**
- Create: `src/llm/HttpTransport.h`, `src/llm/HttpTransport.cpp`
- Test: `tests/HttpTransportTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/HttpTransportTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "llm/HttpTransport.h"
#include "core/Errors.h"

using namespace aicoder;

// 不支持的协议 → curl 在解析阶段就失败（CURLE_UNSUPPORTED_PROTOCOL）→ LlmError。
// 解析期失败先于任何代理/网络，因此与环境中的 HTTP(S)_PROXY 无关，确定性。
// 注意：不要用 "not-a-valid-url" —— libcurl 会补成 http://not-a-valid-url，
// 若环境设了代理则会被路由到代理并拿到 502（CURLE_OK），不会抛异常，测试假阴性。
TEST(HttpTransport, UnsupportedProtocolThrowsLlmError) {
  HttpTransport t;
  EXPECT_THROW(t.post("htp://bad", "{}", {"Content-Type: application/json"}),
               LlmError);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `llm/HttpTransport.h` 不存在。

- [ ] **Step 3: 写 `src/llm/HttpTransport.h`**

```cpp
#pragma once
#include "llm/Transport.h"

namespace aicoder {
class HttpTransport : public Transport {
public:
  HttpTransport();   // 首次构造时一次性 curl_global_init
  HttpResponse post(const std::string& url,
                    const std::string& body,
                    const std::vector<std::string>& headers) override;
};
}
```

- [ ] **Step 4: 写 `src/llm/HttpTransport.cpp`**

```cpp
#include "llm/HttpTransport.h"
#include "core/Errors.h"
#include <curl/curl.h>
#include <mutex>

namespace aicoder {

namespace {
std::once_flag g_curlInit;
size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}
}

HttpTransport::HttpTransport() {
  std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse HttpTransport::post(const std::string& url,
                                 const std::string& body,
                                 const std::vector<std::string>& headers) {
  CURL* curl = curl_easy_init();
  if (!curl) throw LlmError("curl 初始化失败");

  std::string response;
  curl_slist* hdrs = nullptr;
  for (const auto& h : headers) hdrs = curl_slist_append(hdrs, h.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK)
    throw LlmError(std::string("HTTP 传输失败: ") + curl_easy_strerror(rc));
  return HttpResponse{status, response};
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='HttpTransport.*'`
Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 6: 提交**

```bash
git add src/llm/HttpTransport.h src/llm/HttpTransport.cpp tests/HttpTransportTest.cpp
git commit -m "feat: libcurl-backed HttpTransport"
```

---

## Task 9: DefaultLlmClient（编排）

**Files:**
- Create: `src/llm/DefaultLlmClient.h`, `src/llm/DefaultLlmClient.cpp`
- Modify: 需要 `Config` 类型——本任务先用一个轻量内联结构占位？否。本任务依赖 Task 10 的 `Config`。**调整顺序：先做 Task 10（Config）再回到这里。** 见下方"依赖说明"。

> **依赖说明:** DefaultLlmClient 需要 `Config`（取 model/base_url/api_key）。**实现顺序：Task 10（Config）→ Task 9（本任务）。** 计划中编号保持，但执行时先做 Task 10。

- [ ] **Step 1: 写失败测试 `tests/DefaultLlmClientTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <memory>
#include "llm/DefaultLlmClient.h"
#include "llm/OpenAIProvider.h"
#include "llm/Transport.h"
#include "config/Config.h"
#include "core/Errors.h"

using namespace aicoder;

namespace {
class FakeTransport : public Transport {
public:
  HttpResponse next;
  std::string lastBody;
  HttpResponse post(const std::string&, const std::string& body,
                    const std::vector<std::string>&) override {
    lastBody = body;
    return next;
  }
};
Config testConfig() {
  Config c;
  c.api_key = "k";
  c.base_url = "https://example.com/v1";
  c.model = "deepseek-chat";
  c.max_iterations = 16;
  return c;
}
}

TEST(DefaultLlmClient, SuccessDecodesResponse) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{200, R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"嗨"}}]})"};
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));

  Response r = client.send({userText("hi")}, {});
  EXPECT_EQ(assistantText(r.assistant_message), "嗨");
}

TEST(DefaultLlmClient, Non2xxThrowsLlmError) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{500, "server error"};
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));
  EXPECT_THROW(client.send({userText("hi")}, {}), LlmError);
}

TEST(DefaultLlmClient, BadJsonThrowsLlmError) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{200, "not json at all"};
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));
  EXPECT_THROW(client.send({userText("hi")}, {}), LlmError);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `llm/DefaultLlmClient.h` 不存在。

- [ ] **Step 3: 写 `src/llm/DefaultLlmClient.h`**

```cpp
#pragma once
#include <memory>
#include "llm/LlmClient.h"
#include "llm/Provider.h"
#include "llm/Transport.h"
#include "config/Config.h"

namespace aicoder {
class DefaultLlmClient : public LlmClient {
public:
  DefaultLlmClient(Config config,
                   std::unique_ptr<Provider> provider,
                   std::unique_ptr<Transport> transport);
  Response send(const std::vector<Message>& messages,
                const std::vector<ToolSpec>& tools) override;
private:
  Config config_;
  std::unique_ptr<Provider> provider_;
  std::unique_ptr<Transport> transport_;
};
}
```

- [ ] **Step 4: 写 `src/llm/DefaultLlmClient.cpp`**

```cpp
#include "llm/DefaultLlmClient.h"
#include "core/Errors.h"

namespace aicoder {

DefaultLlmClient::DefaultLlmClient(Config config,
                                   std::unique_ptr<Provider> provider,
                                   std::unique_ptr<Transport> transport)
    : config_(std::move(config)),
      provider_(std::move(provider)),
      transport_(std::move(transport)) {}

Response DefaultLlmClient::send(const std::vector<Message>& messages,
                                const std::vector<ToolSpec>& tools) {
  json reqBody = provider_->encodeRequest(messages, tools, config_.model);
  std::string url = config_.base_url + "/chat/completions";
  std::vector<std::string> headers = {
    "Content-Type: application/json",
    "Authorization: Bearer " + config_.api_key
  };

  HttpResponse resp = transport_->post(url, reqBody.dump(), headers);
  if (resp.status < 200 || resp.status >= 300)
    throw LlmError("HTTP " + std::to_string(resp.status) + ": " + resp.body);

  try {
    json body = json::parse(resp.body);
    return provider_->decodeResponse(body);
  } catch (const json::exception& e) {
    throw LlmError(std::string("响应解析失败: ") + e.what());
  }
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='DefaultLlmClient.*'`
Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/llm/DefaultLlmClient.h src/llm/DefaultLlmClient.cpp tests/DefaultLlmClientTest.cpp
git commit -m "feat: DefaultLlmClient orchestrating provider + transport"
```

---

## Task 10: Config（fromEnv，fail-fast）

> **执行顺序：本任务在 Task 9 之前完成**（DefaultLlmClient 依赖 Config）。

**Files:**
- Create: `src/config/Config.h`, `src/config/Config.cpp`
- Test: `tests/ConfigTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/ConfigTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <cstdlib>
#include "config/Config.h"
#include "core/Errors.h"

using namespace aicoder;

TEST(Config, MissingApiKeyThrows) {
  ::unsetenv("AICODER_API_KEY");
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}

TEST(Config, DefaultsWhenOnlyKeySet) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::unsetenv("AICODER_BASE_URL");
  ::unsetenv("AICODER_MODEL");
  ::unsetenv("AICODER_MAX_ITERATIONS");
  Config c = Config::fromEnv();
  EXPECT_EQ(c.api_key, "secret");
  EXPECT_EQ(c.base_url, "https://api.deepseek.com/v1");
  EXPECT_EQ(c.model, "deepseek-v4-pro");
  EXPECT_EQ(c.max_iterations, 16);
}

TEST(Config, OverridesFromEnv) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_BASE_URL", "https://x/v1", 1);
  ::setenv("AICODER_MODEL", "qwen-max", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "5", 1);
  Config c = Config::fromEnv();
  EXPECT_EQ(c.base_url, "https://x/v1");
  EXPECT_EQ(c.model, "qwen-max");
  EXPECT_EQ(c.max_iterations, 5);
}

TEST(Config, NonNumericMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "abc", 1);
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}

TEST(Config, NonPositiveMaxIterationsThrows) {
  ::setenv("AICODER_API_KEY", "secret", 1);
  ::setenv("AICODER_MAX_ITERATIONS", "0", 1);
  EXPECT_THROW(Config::fromEnv(), ConfigError);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `config/Config.h` 不存在。

- [ ] **Step 3: 写 `src/config/Config.h`**

```cpp
#pragma once
#include <string>

namespace aicoder {
struct Config {
  std::string api_key;
  std::string base_url = "https://api.deepseek.com/v1";
  std::string model = "deepseek-chat";
  int max_iterations = 16;

  static Config fromEnv();   // 缺 AICODER_API_KEY 抛 ConfigError
};
}
```

- [ ] **Step 4: 写 `src/config/Config.cpp`**

```cpp
#include "config/Config.h"
#include "core/Errors.h"
#include <cstdlib>
#include <string>

namespace aicoder {

static std::string envOr(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

Config Config::fromEnv() {
  Config c;
  const char* key = std::getenv("AICODER_API_KEY");
  if (!key || !*key)
    throw ConfigError("请设置环境变量 AICODER_API_KEY");
  c.api_key = key;
  // fallback 用结构体内的默认值（单一事实来源），避免在此重复字面量
  c.base_url = envOr("AICODER_BASE_URL", c.base_url);
  c.model = envOr("AICODER_MODEL", c.model);
  std::string mi = envOr("AICODER_MAX_ITERATIONS", std::to_string(c.max_iterations));
  try {
    size_t pos = 0;
    c.max_iterations = std::stoi(mi, &pos);
    if (pos != mi.size()) throw std::invalid_argument(mi);
  } catch (const std::exception&) {
    throw ConfigError("AICODER_MAX_ITERATIONS 必须是整数，当前为: " + mi);
  }
  if (c.max_iterations <= 0)
    throw ConfigError("AICODER_MAX_ITERATIONS 必须是正整数，当前为: " + mi);
  return c;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='Config.*'`
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/config/Config.h src/config/Config.cpp tests/ConfigTest.cpp
git commit -m "feat: Config::fromEnv with fail-fast on missing API key"
```

---

## Task 11: CommandRouter（/quit /clear）

**Files:**
- Create: `src/commands/CommandRouter.h`, `src/commands/CommandRouter.cpp`
- Test: `tests/CommandRouterTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/CommandRouterTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "commands/CommandRouter.h"

using namespace aicoder;

TEST(CommandRouter, QuitReturnsQuit) {
  CommandRouter r;
  std::vector<Message> msgs;
  EXPECT_EQ(r.handle("/quit", msgs), CommandResult::Quit);
}

TEST(CommandRouter, ClearKeepsOnlySystemMessages) {
  CommandRouter r;
  std::vector<Message> msgs = {systemText("sys"), userText("a"),
                               Message{Role::Assistant, {TextBlock{"b"}}}};
  EXPECT_EQ(r.handle("/clear", msgs), CommandResult::Cleared);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0].role, Role::System);
}

TEST(CommandRouter, NormalTextIsNotACommand) {
  CommandRouter r;
  std::vector<Message> msgs = {systemText("sys")};
  EXPECT_EQ(r.handle("hello", msgs), CommandResult::NotACommand);
  EXPECT_EQ(msgs.size(), 1u);   // 未改动
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `commands/CommandRouter.h` 不存在。

- [ ] **Step 3: 写 `src/commands/CommandRouter.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "core/Message.h"

namespace aicoder {

enum class CommandResult { Quit, Cleared, NotACommand };

class CommandRouter {
public:
  // 命中命令则处理（/clear 会改 messages）并返回结果；否则 NotACommand。
  CommandResult handle(const std::string& input, std::vector<Message>& messages) const;
};

}
```

- [ ] **Step 4: 写 `src/commands/CommandRouter.cpp`**

```cpp
#include "commands/CommandRouter.h"

namespace aicoder {

CommandResult CommandRouter::handle(const std::string& input,
                                    std::vector<Message>& messages) const {
  if (input == "/quit" || input == "/exit")
    return CommandResult::Quit;
  if (input == "/clear") {
    std::vector<Message> kept;
    for (const auto& m : messages)
      if (m.role == Role::System) kept.push_back(m);
    messages = std::move(kept);
    return CommandResult::Cleared;
  }
  return CommandResult::NotACommand;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='CommandRouter.*'`
Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/commands/CommandRouter.h src/commands/CommandRouter.cpp tests/CommandRouterTest.cpp
git commit -m "feat: CommandRouter for /quit and /clear"
```

---

## Task 12: AgentLoop（迭代循环）⭐

**Files:**
- Create: `src/core/AgentLoop.h`, `src/core/AgentLoop.cpp`
- Test: `tests/AgentLoopTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/AgentLoopTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <functional>
#include "core/AgentLoop.h"
#include "core/ToolRegistry.h"
#include "core/Errors.h"
#include "llm/LlmClient.h"

using namespace aicoder;

namespace {
// 由测试脚本控制每次 send 返回什么（按调用序号）
class FakeLlmClient : public LlmClient {
public:
  explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}
  Response send(const std::vector<Message>&, const std::vector<ToolSpec>&) override {
    return fn_(calls_++);
  }
  int calls_ = 0;
private:
  std::function<Response(int)> fn_;
};

Response textResp(std::string t) {
  return Response{Message{Role::Assistant, {TextBlock{std::move(t)}}}, "stop"};
}
Response toolResp(std::string id, std::string name, json input) {
  return Response{Message{Role::Assistant, {ToolUseBlock{std::move(id), std::move(name), std::move(input)}}},
                  "tool_calls"};
}
ToolRegistry registryWith(Tool t) {
  ToolRegistry r;
  r.registerTool(std::move(t));
  return r;
}
Tool echoTool() {
  Tool t; t.name = "echo"; t.description = "echo";
  t.input_schema = json{{"type", "object"}};
  t.execute = [](const json& in) { return in.value("v", std::string("?")); };
  return t;
}
}

TEST(AgentLoop, TextOnlyStopsAfterOneCall) {
  FakeLlmClient fake([](int) { return textResp("done"); });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("hi")};
  std::string reply = loop.run(msgs);
  EXPECT_EQ(reply, "done");
  EXPECT_EQ(fake.calls_, 1);
  EXPECT_EQ(msgs.back().role, Role::Assistant);
}

TEST(AgentLoop, ToolUseThenTextFeedsResultBack) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "echo", json{{"v", "RESULT"}}) : textResp("final");
  });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("use tool")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(reply, "final");
  EXPECT_EQ(fake.calls_, 2);
  // 找到回灌的 tool result
  bool found = false;
  for (const auto& m : msgs)
    if (m.role == Role::Tool)
      for (const auto& b : m.content)
        if (auto* tr = std::get_if<ToolResultBlock>(&b)) {
          EXPECT_EQ(tr->content, "RESULT");
          EXPECT_FALSE(tr->is_error);
          found = true;
        }
  EXPECT_TRUE(found);
}

TEST(AgentLoop, ToolErrorIsFedBackAndLoopContinues) {
  FakeLlmClient fake([](int i) {
    return i == 0 ? toolResp("c1", "ghost", json::object()) : textResp("recovered");
  });
  ToolRegistry reg;   // 没有名为 ghost 的工具
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("x")};
  std::string reply = loop.run(msgs);

  EXPECT_EQ(reply, "recovered");
  bool sawError = false;
  for (const auto& m : msgs)
    if (m.role == Role::Tool)
      for (const auto& b : m.content)
        if (auto* tr = std::get_if<ToolResultBlock>(&b))
          if (tr->is_error) sawError = true;
  EXPECT_TRUE(sawError);
}

TEST(AgentLoop, HitsMaxIterationsGracefully) {
  FakeLlmClient fake([](int) { return toolResp("c", "echo", json{{"v", "x"}}); });
  ToolRegistry reg = registryWith(echoTool());
  AgentLoop loop(fake, reg, 3);
  std::vector<Message> msgs = {userText("loop forever")};
  std::string reply = loop.run(msgs);   // 不应抛异常

  EXPECT_EQ(fake.calls_, 3);
  EXPECT_NE(reply.find("最大迭代"), std::string::npos);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `core/AgentLoop.h` 不存在。

- [ ] **Step 3: 写 `src/core/AgentLoop.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "core/Message.h"
#include "core/ToolRegistry.h"
#include "llm/LlmClient.h"

namespace aicoder {

class AgentLoop {
public:
  AgentLoop(LlmClient& client, const ToolRegistry& registry, int maxIterations);
  // 迭代直到模型不再要工具（end_turn）或撞上限；追加消息到 messages；返回最终 assistant 文本。
  // 基建失败时 client.send 抛 LlmError，由调用方处理。
  std::string run(std::vector<Message>& messages);
private:
  LlmClient& client_;
  const ToolRegistry& registry_;
  int maxIterations_;
};

}
```

- [ ] **Step 4: 写 `src/core/AgentLoop.cpp`**

```cpp
#include "core/AgentLoop.h"

namespace aicoder {

AgentLoop::AgentLoop(LlmClient& client, const ToolRegistry& registry, int maxIterations)
    : client_(client), registry_(registry), maxIterations_(maxIterations) {}

std::string AgentLoop::run(std::vector<Message>& messages) {
  for (int iter = 0; iter < maxIterations_; ++iter) {
    Response resp = client_.send(messages, registry_.specs());
    messages.push_back(resp.assistant_message);

    std::vector<ToolUseBlock> toolUses;
    for (const auto& block : resp.assistant_message.content)
      if (const auto* tu = std::get_if<ToolUseBlock>(&block))
        toolUses.push_back(*tu);

    if (toolUses.empty())
      return assistantText(resp.assistant_message);   // end_turn

    Message toolMsg{Role::Tool, {}};
    for (const auto& tu : toolUses)
      toolMsg.content.push_back(registry_.invoke(tu.id, tu.name, tu.input));
    messages.push_back(std::move(toolMsg));
  }

  const std::string note = "[已达到最大迭代次数，已停止]";
  messages.push_back(Message{Role::Assistant, {TextBlock{note}}});
  return note;
}

}
```

- [ ] **Step 5: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='AgentLoop.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/core/AgentLoop.h src/core/AgentLoop.cpp tests/AgentLoopTest.cpp
git commit -m "feat: AgentLoop iterative tool-calling loop with maxIterations guard"
```

---

## Task 13: ConsoleRepl + main 装配

**Files:**
- Create: `src/ui/ConsoleRepl.h`, `src/ui/ConsoleRepl.cpp`
- Modify: `src/main.cpp`（替换 Task 1 的占位）
- Test: `tests/ConsoleReplTest.cpp`

- [ ] **Step 1: 写失败测试 `tests/ConsoleReplTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <sstream>
#include <functional>
#include "ui/ConsoleRepl.h"
#include "core/AgentLoop.h"
#include "core/ToolRegistry.h"
#include "commands/CommandRouter.h"
#include "llm/LlmClient.h"

using namespace aicoder;

namespace {
class FakeLlmClient : public LlmClient {
public:
  explicit FakeLlmClient(std::string reply) : reply_(std::move(reply)) {}
  Response send(const std::vector<Message>&, const std::vector<ToolSpec>&) override {
    return Response{Message{Role::Assistant, {TextBlock{reply_}}}, "stop"};
  }
private:
  std::string reply_;
};
}

TEST(ConsoleRepl, PrintsAssistantReplyThenQuits) {
  FakeLlmClient fake("机器人回答");
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  CommandRouter router;
  std::istringstream in("你好\n/quit\n");
  std::ostringstream out;
  ConsoleRepl repl(loop, router, "sys", in, out);
  repl.run();
  EXPECT_NE(out.str().find("机器人回答"), std::string::npos);
}

TEST(ConsoleRepl, ClearCommandPrintsNotice) {
  FakeLlmClient fake("x");
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  CommandRouter router;
  std::istringstream in("/clear\n/quit\n");
  std::ostringstream out;
  ConsoleRepl repl(loop, router, "sys", in, out);
  repl.run();
  EXPECT_NE(out.str().find("已清空"), std::string::npos);
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `ui/ConsoleRepl.h` 不存在。

- [ ] **Step 3: 写 `src/ui/ConsoleRepl.h`**

```cpp
#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "core/AgentLoop.h"
#include "core/Message.h"
#include "commands/CommandRouter.h"

namespace aicoder {

class ConsoleRepl {
public:
  ConsoleRepl(AgentLoop& loop, const CommandRouter& router, std::string systemPrompt,
              std::istream& in = std::cin, std::ostream& out = std::cout);
  void run();
private:
  AgentLoop& loop_;
  const CommandRouter& router_;
  std::string systemPrompt_;
  std::istream& in_;
  std::ostream& out_;
  std::vector<Message> messages_;
};

}
```

- [ ] **Step 4: 写 `src/ui/ConsoleRepl.cpp`**

```cpp
#include "ui/ConsoleRepl.h"
#include "core/Errors.h"

namespace aicoder {

ConsoleRepl::ConsoleRepl(AgentLoop& loop, const CommandRouter& router,
                         std::string systemPrompt, std::istream& in, std::ostream& out)
    : loop_(loop), router_(router), systemPrompt_(std::move(systemPrompt)),
      in_(in), out_(out) {}

void ConsoleRepl::run() {
  messages_ = {systemText(systemPrompt_)};
  out_ << "AICoder SP1a — 输入消息，/clear 清空对话，/quit 退出\n";
  std::string line;
  while (true) {
    out_ << "> " << std::flush;
    if (!std::getline(in_, line)) break;   // EOF
    if (line.empty()) continue;

    CommandResult cr = router_.handle(line, messages_);
    if (cr == CommandResult::Quit) break;
    if (cr == CommandResult::Cleared) { out_ << "[已清空对话]\n"; continue; }

    messages_.push_back(userText(line));
    try {
      std::string reply = loop_.run(messages_);
      out_ << reply << "\n";
    } catch (const LlmError& e) {
      out_ << "[错误] " << e.what() << "\n";
    }
  }
}

}
```

- [ ] **Step 5: 替换 `src/main.cpp`**

```cpp
#include <iostream>
#include <memory>
#include "config/Config.h"
#include "core/Errors.h"
#include "core/ToolRegistry.h"
#include "core/AgentLoop.h"
#include "commands/CommandRouter.h"
#include "llm/DefaultLlmClient.h"
#include "llm/OpenAIProvider.h"
#include "llm/HttpTransport.h"
#include "tools/ReadFileTool.h"
#include "tools/ListDirTool.h"
#include "ui/ConsoleRepl.h"

using namespace aicoder;

int main() {
  Config config;
  try {
    config = Config::fromEnv();
  } catch (const ConfigError& e) {
    std::cerr << "[配置错误] " << e.what() << "\n";
    return 1;
  }

  ToolRegistry registry;
  registry.registerTool(makeReadFileTool());
  registry.registerTool(makeListDirTool());

  DefaultLlmClient client(config,
                          std::make_unique<OpenAIProvider>(),
                          std::make_unique<HttpTransport>());
  AgentLoop loop(client, registry, config.max_iterations);
  CommandRouter router;

  std::string systemPrompt =
      "你是 AICoder，一个运行在命令行的编码助手。"
      "你可以调用 read_file 读取文件、list_dir 列出目录。"
      "需要文件或目录信息时请主动调用工具，不要编造内容。";

  ConsoleRepl repl(loop, router, systemPrompt);
  repl.run();
  return 0;
}
```

- [ ] **Step 6: 运行确认通过**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='ConsoleRepl.*'`
Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 7: 全量测试**

Run: `ctest --test-dir build --output-on-failure`
Expected: 所有测试通过（约 30+ 个）。

- [ ] **Step 8: 提交**

```bash
git add src/ui/ConsoleRepl.h src/ui/ConsoleRepl.cpp src/main.cpp tests/ConsoleReplTest.cpp
git commit -m "feat: ConsoleRepl + main wiring (SP1a complete)"
```

---

## Task 14: 端到端人工验收（真实 API）

无自动化测试——这是 SP1a 的验收闸。需要真实的 OpenAI 兼容 API key。

- [ ] **Step 1: 设置环境变量**

```bash
export AICODER_API_KEY=<你的 DeepSeek/Qwen key>
# 可选： export AICODER_BASE_URL=https://api.deepseek.com/v1
# 可选： export AICODER_MODEL=deepseek-chat
```

- [ ] **Step 2: 验收 — 缺 key 时 fail-fast**

```bash
env -u AICODER_API_KEY ./build/aicoder
```
Expected: 打印 `[配置错误] 请设置环境变量 AICODER_API_KEY`，退出码 1。

- [ ] **Step 3: 验收 — 多轮纯对话**

Run: `./build/aicoder`，输入 `你好，简单介绍你自己`
Expected: 得到模型文本回复。

- [ ] **Step 4: 验收 — ReadFile 工具闭环**

输入：`读一下 ./CMakeLists.txt 讲了什么`
Expected: 模型发起 read_file 工具调用 → 程序读取 → 模型基于真实文件内容总结。

- [ ] **Step 5: 验收 — ListDir 工具闭环**

输入：`列一下 ./src 目录里有什么`
Expected: 模型走 list_dir → 得到条目 → 基于真实列表回答。

- [ ] **Step 6: 验收 — 工具失败回灌**

输入：`读一下 ./不存在的文件.txt`
Expected: 模型收到错误 result，能解释文件不存在或换路径，而非程序崩溃。

- [ ] **Step 7: 验收 — 命令**

输入 `/clear` → 显示 `[已清空对话]`；输入 `/quit` → 干净退出。

- [ ] **Step 8: 记录验收结果**

在 commit message 或 `docs/` 记录验收通过。
```bash
git commit --allow-empty -m "test: SP1a manual acceptance passed (real API end-to-end)"
```

---

## Self-Review（计划作者已核对）

**Spec coverage:**
- canonical Message 模型 → Task 2 ✓
- Tool / ToolRegistry / 错误包装 → Task 3 ✓
- 两个只读工具 ReadFile/ListDir → Task 4, 5 ✓
- LLM 三层（Transport/Provider/DefaultLlmClient）→ Task 6, 7, 8, 9 ✓
- canonical ⇄ OpenAI tool_calls 编解码 → Task 7 ✓
- Config fail-fast 密钥 → Task 10 ✓
- /quit /clear 命令 → Task 11 ✓
- 迭代循环 + 终止 + maxIterations + 工具失败回灌 + 基建失败中断 → Task 12（循环/上限/工具回灌）、Task 9（基建失败抛 LlmError）、Task 13（ConsoleRepl 捕获 LlmError 显示）✓
- 控制台 REPL（可注入 stream 以可测）→ Task 13 ✓
- 分段验收 SP1a → Task 14 ✓
- 测试套件（ToolRegistry/OpenAIProvider/AgentLoop 等）→ 各任务 ✓

**类型一致性核对:** `Response{assistant_message, finish_reason}`、`ToolSpec{name,description,input_schema}`、`ToolRegistry::specs()/invoke(id,name,input)→ToolResultBlock`、`LlmClient::send(messages,tools)`、`AgentLoop(LlmClient&, const ToolRegistry&, int)`、`CommandRouter::handle(string, vector<Message>&)→CommandResult`、`Config{api_key,base_url,model,max_iterations}` —— 跨任务签名一致。

**执行顺序提醒:** Task 10（Config）需在 Task 9（DefaultLlmClient）之前完成；其余按编号顺序。

---

## Phase 2 — SP1b（独立计划，SP1a 验收后再写）

SP1a 通过 Task 14 后，针对真实代码再写 `2026-XX-XX-aicoder-sp1b.md`，覆盖：
- 引入 FTXUI（CMake FetchContent 或系统包）。
- `ReplView`：消息列表 + 输入框。
- worker 线程跑 `AgentLoop::run`，`ScreenInteractive::Post()` 回 UI 线程刷新。
- 「思考中」输入框置灰，禁止并发提交。
- 验收：与 SP1a 完全相同的对话/工具/命令行为（证明内核零改动）。

> 内核（core/llm/tools/config/commands）在 SP1b 中**不改一行**——只新增 `ui/ReplView`，`main.cpp` 切换启动 ConsoleRepl → ReplView。
