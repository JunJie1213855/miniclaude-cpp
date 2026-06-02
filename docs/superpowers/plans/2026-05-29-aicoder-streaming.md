# AICoder 流式输出 实现计划（SSE streaming）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 LLM 调用从「一次性收完」改为「SSE 流式」。TUI 边收边显示（content + reasoning 增量），控制台保持收完再显示（但底层也走流式协议）。AgentLoop 仍在流结束后拿到完整 `Response`，工具调用逻辑不变。

**Architecture:** 新增 `StreamDelta`（显示增量）+ `OpenAIStreamParser`（SSE 累积状态机，跨 chunk 拼接 content/reasoning_content/分片 tool_calls）。`Transport` 加 `postStream`（libcurl write callback 逐块回调）。`LlmClient::sendStream` 成为主方法，`send()` 退化为 noop-回调包装。`AgentLoop::run` 加可选 `onDelta` 回调。TUI 的 onDelta 经 `screen.Post()` 把增量喂给 `ReplView` 的流式气泡。

**Tech Stack:** C++20 · libcurl（CURLOPT_WRITEFUNCTION 流式）· nlohmann/json（解析每条 SSE data 行）· FTXUI · GoogleTest。

**关键约束：** 工具调用阶段（执行 read_file/list_dir）不是流式；只有模型「文字 + 思考」是流式。`OpenAIProvider` 的 `encodeRequest`/`decodeResponse` **不改**（保留其单测）；流式解码用新的 `OpenAIStreamParser`。

**工作目录:** `~/lib/CppCode/AICoder/`（当前 43 tests 通过）。

---

## SSE 协议速览（DeepSeek / OpenAI 兼容）

请求体加 `"stream": true`。响应是一串 `text/event-stream`：

```
data: {"choices":[{"delta":{"role":"assistant","content":""}}]}

data: {"choices":[{"delta":{"reasoning_content":"我先"}}]}

data: {"choices":[{"delta":{"reasoning_content":"想想"}}]}

data: {"choices":[{"delta":{"content":"你"}}]}

data: {"choices":[{"delta":{"content":"好"}}]}

data: {"choices":[{"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

工具调用是**分片**的（按 `index` 累积，`arguments` 字符串逐片拼接）：

```
data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read_file","arguments":""}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"pa"}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"th\":\"x\"}"}}]}}]}

data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]}

data: [DONE]
```

注意：一个网络 chunk 可能包含多条 `data:` 行，也可能在某行中间**截断**（半行）。解析器必须缓存残留半行，跨 chunk 拼接。

---

## 文件结构

| 文件 | 职责 | 状态 |
|---|---|---|
| `src/llm/StreamDelta.h` | `StreamDelta{text, reasoning}` 显示增量结构 | 新增 |
| `src/llm/StreamParser.h/.cpp` | `OpenAIStreamParser`：SSE 累积状态机 | 新增 |
| `src/llm/LlmClient.h` | 加 `DeltaCallback` + `sendStream` 纯虚 + `send` 包装 | 修改 |
| `src/llm/Transport.h` | 加 `ChunkCallback` + `postStream` | 修改 |
| `src/llm/HttpTransport.h/.cpp` | 实现 `postStream`（流式 write callback） | 修改 |
| `src/llm/DefaultLlmClient.h/.cpp` | 实现 `sendStream`（stream:true + postStream + parser），删 `send` override | 修改 |
| `src/core/AgentLoop.h/.cpp` | `run()` 加可选 `onDelta`，改调 `sendStream` | 修改 |
| `src/ui/ReplView.h/.cpp` | 流式气泡：`appendDelta` + 渲染增长中的回复 | 修改 |
| `src/ui/App.cpp` | `onSubmit` 把 onDelta 传给 `loop.run` | 修改 |
| `tests/StreamParserTest.cpp` | SSE 解析单测（含跨 chunk、tool_calls 累积、reasoning） | 新增 |
| `tests/DefaultLlmClientTest.cpp` | FakeTransport 改 `postStream` 喂 SSE | 修改 |
| `tests/AgentLoopTest.cpp` | FakeLlmClient 改 `sendStream` + 加 delta 测试 | 修改 |
| `tests/ConsoleReplTest.cpp` | FakeLlmClient 改 `sendStream` | 修改 |
| `tests/HttpTransportTest.cpp` | 改测 `postStream` | 修改 |

---

## Task 1: StreamDelta + OpenAIStreamParser（核心 SSE 状态机）

**Files:**
- Create: `src/llm/StreamDelta.h`, `src/llm/StreamParser.h`, `src/llm/StreamParser.cpp`
- Test: `tests/StreamParserTest.cpp`

- [ ] **Step 1: 写 `src/llm/StreamDelta.h`**

```cpp
#pragma once
#include <string>

namespace aicoder {
// 流式显示增量：仅供 UI 实时展示，不参与工具调用判断。
struct StreamDelta {
  std::string text;       // content 增量（最终答复正文）
  std::string reasoning;  // reasoning_content 增量（思维链）
};
}
```

- [ ] **Step 2: 写失败测试 `tests/StreamParserTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "llm/StreamParser.h"

using namespace aicoder;

namespace {
// 收集 onDelta 回调
struct Collector {
  std::string text;
  std::string reasoning;
  void operator()(const StreamDelta& d) { text += d.text; reasoning += d.reasoning; }
};
}

TEST(StreamParser, AccumulatesPlainTextAcrossChunks) {
  OpenAIStreamParser p;
  Collector c;
  // 两条 data 行在一个 chunk
  p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n\n"
         "data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n",
         std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n", std::ref(c));
  p.feed("data: [DONE]\n\n", std::ref(c));

  EXPECT_EQ(c.text, "你好");
  Response r = p.finish();
  EXPECT_EQ(r.finish_reason, "stop");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tb = std::get_if<TextBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tb, nullptr);
  EXPECT_EQ(tb->text, "你好");
}

TEST(StreamParser, HandlesLineSplitAcrossChunks) {
  OpenAIStreamParser p;
  Collector c;
  // 一行被截成两个 chunk
  p.feed("data: {\"choices\":[{\"delta\":{\"cont", std::ref(c));
  p.feed("ent\":\"X\"}}]}\n\n", std::ref(c));
  EXPECT_EQ(c.text, "X");
}

TEST(StreamParser, AccumulatesReasoningContent) {
  OpenAIStreamParser p;
  Collector c;
  p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"答\"}}]}\n\n", std::ref(c));
  EXPECT_EQ(c.reasoning, "想想");
  EXPECT_EQ(c.text, "答");
  Response r = p.finish();
  EXPECT_EQ(r.reasoning_content, "想想");
}

TEST(StreamParser, AccumulatesStreamedToolCall) {
  OpenAIStreamParser p;
  Collector c;
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
         "\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"\"}}]}}]}\n\n",
         std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"function\":{\"arguments\":\"{\\\"pa\"}}]}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
         "\"function\":{\"arguments\":\"th\\\":\\\"x\\\"}\"}}]}}]}\n\n", std::ref(c));
  p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n", std::ref(c));

  Response r = p.finish();
  EXPECT_EQ(r.finish_reason, "tool_calls");
  ASSERT_EQ(r.assistant_message.content.size(), 1u);
  auto* tu = std::get_if<ToolUseBlock>(&r.assistant_message.content[0]);
  ASSERT_NE(tu, nullptr);
  EXPECT_EQ(tu->id, "call_1");
  EXPECT_EQ(tu->name, "read_file");
  EXPECT_EQ(tu->input["path"].get<std::string>(), "x");
}
```

- [ ] **Step 3: 运行确认失败**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: `llm/StreamParser.h` 不存在。

- [ ] **Step 4: 写 `src/llm/StreamParser.h`**

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "llm/Response.h"
#include "llm/StreamDelta.h"

namespace aicoder {

// OpenAI/DeepSeek 兼容的 SSE 流式解析器。
// feed() 可被多次调用，每次喂入任意一段原始 SSE 字节（可能含多条/半条 data 行）。
// 解析出的可显示增量通过 onDelta 实时吐出；同时内部累积完整消息，
// 流结束后由 finish() 产出与非流式 decodeResponse 等价的 Response。
class OpenAIStreamParser {
public:
  using DeltaFn = std::function<void(const StreamDelta&)>;

  void feed(const std::string& chunk, const DeltaFn& onDelta);
  Response finish() const;

private:
  void processLine(const std::string& line, const DeltaFn& onDelta);

  std::string buffer_;          // 跨 chunk 残留的半行
  std::string content_;
  std::string reasoning_;
  std::string finish_reason_;

  struct ToolAccum { std::string id, name, args; };
  std::vector<ToolAccum> tools_;  // 按 index 累积
};

}
```

- [ ] **Step 5: 写 `src/llm/StreamParser.cpp`**

```cpp
#include "llm/StreamParser.h"

namespace aicoder {

void OpenAIStreamParser::feed(const std::string& chunk, const DeltaFn& onDelta) {
  buffer_ += chunk;
  // 按 '\n' 切出完整行，残留半行留在 buffer_
  size_t pos;
  while ((pos = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    // 去掉行尾可能的 '\r'
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) processLine(line, onDelta);
  }
}

void OpenAIStreamParser::processLine(const std::string& line, const DeltaFn& onDelta) {
  // 只处理 "data: " 开头的行；其余（event:/空注释）忽略
  const std::string prefix = "data: ";
  if (line.rfind(prefix, 0) != 0) return;
  std::string payload = line.substr(prefix.size());
  if (payload == "[DONE]") return;

  json j;
  try {
    j = json::parse(payload);
  } catch (const json::exception&) {
    return;  // 跳过无法解析的行（容错）
  }
  if (!j.contains("choices") || j["choices"].empty()) return;
  const auto& choice = j["choices"][0];

  if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
    finish_reason_ = choice["finish_reason"].get<std::string>();

  if (!choice.contains("delta")) return;
  const auto& delta = choice["delta"];

  StreamDelta out;
  if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
    std::string piece = delta["reasoning_content"].get<std::string>();
    reasoning_ += piece;
    out.reasoning = piece;
  }
  if (delta.contains("content") && delta["content"].is_string()) {
    std::string piece = delta["content"].get<std::string>();
    content_ += piece;
    out.text = piece;
  }
  if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
    for (const auto& tc : delta["tool_calls"]) {
      size_t idx = tc.value("index", 0);
      if (idx >= tools_.size()) tools_.resize(idx + 1);
      ToolAccum& acc = tools_[idx];
      if (tc.contains("id") && tc["id"].is_string())
        acc.id = tc["id"].get<std::string>();
      if (tc.contains("function")) {
        const auto& fn = tc["function"];
        if (fn.contains("name") && fn["name"].is_string())
          acc.name = fn["name"].get<std::string>();
        if (fn.contains("arguments") && fn["arguments"].is_string())
          acc.args += fn["arguments"].get<std::string>();
      }
    }
  }

  if (!out.text.empty() || !out.reasoning.empty()) onDelta(out);
}

Response OpenAIStreamParser::finish() const {
  Response r;
  r.finish_reason = finish_reason_;
  r.reasoning_content = reasoning_;

  Message am;
  am.role = Role::Assistant;
  am.reasoning_content = reasoning_;
  if (!content_.empty()) am.content.push_back(TextBlock{content_});
  for (const auto& acc : tools_) {
    if (acc.name.empty()) continue;
    ToolUseBlock tu;
    tu.id = acc.id;
    tu.name = acc.name;
    tu.input = acc.args.empty() ? json::object() : json::parse(acc.args);
    am.content.push_back(tu);
  }
  r.assistant_message = am;
  return r;
}

}
```

- [ ] **Step 6: 运行确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ./build/aicoder_tests --gtest_filter='StreamParser.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 7: 提交**

```bash
git add src/llm/StreamDelta.h src/llm/StreamParser.h src/llm/StreamParser.cpp tests/StreamParserTest.cpp
git commit -m "feat: OpenAIStreamParser SSE accumulator (content/reasoning/tool_calls)"
```

---

## Task 2: Transport.postStream + HttpTransport 实现

**Files:**
- Modify: `src/llm/Transport.h`, `src/llm/HttpTransport.h`, `src/llm/HttpTransport.cpp`
- Modify: `tests/HttpTransportTest.cpp`

- [ ] **Step 1: 修改 `src/llm/Transport.h`（加 postStream，保留 post）**

完整新内容：
```cpp
#pragma once
#include <functional>
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

  // 非流式：一次性 POST，收完整 body。
  virtual HttpResponse post(const std::string& url,
                            const std::string& body,
                            const std::vector<std::string>& headers) = 0;

  // 流式：每收到一块响应字节就调用 onChunk(原始字节)。
  // 仍返回 HttpResponse（status + 累积的完整 body，供非 2xx 错误报告用）。
  // 传输层失败（连接/超时/URL 错误）抛 LlmError。
  using ChunkCallback = std::function<void(const std::string& chunk)>;
  virtual HttpResponse postStream(const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::string>& headers,
                                  const ChunkCallback& onChunk) = 0;
};
}
```

- [ ] **Step 2: 修改 `src/llm/HttpTransport.h`（加 postStream 声明）**

完整新内容：
```cpp
#pragma once
#include "llm/Transport.h"

namespace aicoder {
class HttpTransport : public Transport {
public:
  HttpTransport();
  HttpResponse post(const std::string& url,
                    const std::string& body,
                    const std::vector<std::string>& headers) override;
  HttpResponse postStream(const std::string& url,
                          const std::string& body,
                          const std::vector<std::string>& headers,
                          const ChunkCallback& onChunk) override;
};
}
```

- [ ] **Step 3: 修改 `src/llm/HttpTransport.cpp`（实现 postStream）**

在文件中现有 `writeCb` 后、`post` 实现后追加。完整新文件：
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

// 流式写回调的上下文：把每块字节既追加到 full_body（错误报告用），
// 又转发给 onChunk（解析用）。
struct StreamCtx {
  std::string* full_body;
  const Transport::ChunkCallback* on_chunk;
};

size_t streamWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* ctx = static_cast<StreamCtx*>(userdata);
  std::string chunk(ptr, size * nmemb);
  ctx->full_body->append(chunk);
  (*ctx->on_chunk)(chunk);
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

HttpResponse HttpTransport::postStream(const std::string& url,
                                       const std::string& body,
                                       const std::vector<std::string>& headers,
                                       const ChunkCallback& onChunk) {
  CURL* curl = curl_easy_init();
  if (!curl) throw LlmError("curl 初始化失败");

  std::string full_body;
  StreamCtx ctx{&full_body, &onChunk};
  curl_slist* hdrs = nullptr;
  for (const auto& h : headers) hdrs = curl_slist_append(hdrs, h.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK)
    throw LlmError(std::string("HTTP 传输失败: ") + curl_easy_strerror(rc));
  return HttpResponse{status, full_body};
}

}
```

- [ ] **Step 4: 修改 `tests/HttpTransportTest.cpp`（加 postStream 测试）**

完整新内容：
```cpp
#include <gtest/gtest.h>
#include "llm/HttpTransport.h"
#include "core/Errors.h"

using namespace aicoder;

// 不支持的协议 → curl 解析期失败 → LlmError（与代理无关，确定性）。
TEST(HttpTransport, UnsupportedProtocolThrowsLlmError) {
  HttpTransport t;
  EXPECT_THROW(t.post("htp://bad", "{}", {"Content-Type: application/json"}),
               LlmError);
}

TEST(HttpTransport, StreamUnsupportedProtocolThrowsLlmError) {
  HttpTransport t;
  bool called = false;
  auto onChunk = [&](const std::string&) { called = true; };
  EXPECT_THROW(t.postStream("htp://bad", "{}", {"Content-Type: application/json"}, onChunk),
               LlmError);
  EXPECT_FALSE(called);  // 解析期就失败，没有数据块
}
```

- [ ] **Step 5: 构建 + 测试**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='HttpTransport.*'`
Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 6: 提交**

```bash
git add src/llm/Transport.h src/llm/HttpTransport.h src/llm/HttpTransport.cpp tests/HttpTransportTest.cpp
git commit -m "feat: Transport.postStream + HttpTransport streaming write callback"
```

---

## Task 3: LlmClient.sendStream + send 包装

**Files:**
- Modify: `src/llm/LlmClient.h`

- [ ] **Step 1: 修改 `src/llm/LlmClient.h`（sendStream 主、send 包装）**

完整新内容：
```cpp
#pragma once
#include <functional>
#include <vector>
#include "core/Message.h"
#include "core/Tool.h"
#include "llm/Response.h"
#include "llm/StreamDelta.h"

namespace aicoder {

// llm 客户端基类，可以通过该基类派生其他的 llm 模型客户端支持
class LlmClient {
public:
  virtual ~LlmClient() = default;

  using DeltaCallback = std::function<void(const StreamDelta&)>;

  // 主方法：流式发送。边收边通过 onDelta 吐出可显示增量，流结束后返回完整 Response。
  // 基建失败（网络/HTTP/解析）抛 LlmError。
  virtual Response sendStream(const std::vector<Message>& messages,
                              const std::vector<ToolSpec>& tools,
                              const DeltaCallback& onDelta) = 0;

  // 便捷包装：非流式语义（不关心增量），内部走流式协议、用 noop 回调收完整结果。
  Response send(const std::vector<Message>& messages,
                const std::vector<ToolSpec>& tools) {
    return sendStream(messages, tools, [](const StreamDelta&) {});
  }
};

}
```

- [ ] **Step 2: 构建（会因 DefaultLlmClient 仍 override send 而失败，下一 Task 修）**

Run: `cmake --build build -j 2>&1 | grep -E "error:" | head`
Expected: 报错 `DefaultLlmClient` 仍声明 `send` override 但基类 send 不再是 virtual。Task 4 修复。**本 Task 不单独提交，与 Task 4 合并提交。**

> **说明：** Task 3 与 Task 4 紧耦合，连续完成后一起提交。

---

## Task 4: DefaultLlmClient.sendStream

**Files:**
- Modify: `src/llm/DefaultLlmClient.h`, `src/llm/DefaultLlmClient.cpp`
- Modify: `tests/DefaultLlmClientTest.cpp`

- [ ] **Step 1: 修改 `src/llm/DefaultLlmClient.h`**

完整新内容：
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
  Response sendStream(const std::vector<Message>& messages,
                      const std::vector<ToolSpec>& tools,
                      const DeltaCallback& onDelta) override;
private:
  Config config_;
  std::unique_ptr<Provider> provider_;
  std::unique_ptr<Transport> transport_;
};
}
```

- [ ] **Step 2: 修改 `src/llm/DefaultLlmClient.cpp`**

完整新内容：
```cpp
#include "llm/DefaultLlmClient.h"
#include "llm/StreamParser.h"
#include "core/Errors.h"

namespace aicoder {

DefaultLlmClient::DefaultLlmClient(Config config,
                                   std::unique_ptr<Provider> provider,
                                   std::unique_ptr<Transport> transport)
    : config_(std::move(config)),
      provider_(std::move(provider)),
      transport_(std::move(transport)) {}

Response DefaultLlmClient::sendStream(const std::vector<Message>& messages,
                                      const std::vector<ToolSpec>& tools,
                                      const DeltaCallback& onDelta) {
  json reqBody = provider_->encodeRequest(messages, tools, config_.model);
  reqBody["stream"] = true;
  std::string url = config_.base_url + "/chat/completions";
  std::vector<std::string> headers = {
    "Content-Type: application/json",
    "Authorization: Bearer " + config_.api_key
  };

  OpenAIStreamParser parser;
  HttpResponse resp = transport_->postStream(
      url, reqBody.dump(), headers,
      [&](const std::string& chunk) {
        parser.feed(chunk, onDelta);
      });

  if (resp.status < 200 || resp.status >= 300)
    throw LlmError("HTTP " + std::to_string(resp.status) + ": " + resp.body);

  return parser.finish();
}

}
```

> **注意：** 流式下解析在 write callback 内进行；若 SSE 内容本身畸形，`parser` 已做容错（跳过坏行）。非 2xx 时 `resp.body` 是累积的错误 body（DeepSeek 错误通常是普通 JSON，不是 SSE），照常抛 LlmError。

- [ ] **Step 3: 修改 `tests/DefaultLlmClientTest.cpp`（FakeTransport 改 postStream 喂 SSE）**

完整新内容：
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
// 假传输：postStream 把预设的 SSE 文本分块喂回去；post 不用（流式路径）。
class FakeTransport : public Transport {
public:
  HttpResponse next;            // status + 这里 body 当作要回放的 SSE 文本
  std::string lastBody;         // 记录请求体（验证 stream:true）

  HttpResponse post(const std::string&, const std::string& body,
                    const std::vector<std::string>&) override {
    lastBody = body;
    return next;
  }
  HttpResponse postStream(const std::string&, const std::string& body,
                          const std::vector<std::string>&,
                          const ChunkCallback& onChunk) override {
    lastBody = body;
    // 一次性把整个 SSE 文本作为一个 chunk 回放（解析器对分块不敏感）
    onChunk(next.body);
    return HttpResponse{next.status, next.body};
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

TEST(DefaultLlmClient, StreamSuccessAccumulatesResponse) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{200,
      "data: {\"choices\":[{\"delta\":{\"content\":\"嗨\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
      "data: [DONE]\n\n"};
  auto* tptr = transport.get();
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));

  std::string streamed;
  Response r = client.sendStream({userText("hi")}, {},
      [&](const StreamDelta& d) { streamed += d.text; });
  EXPECT_EQ(assistantText(r.assistant_message), "嗨");
  EXPECT_EQ(streamed, "嗨");
  // 请求体应带 stream:true
  EXPECT_NE(tptr->lastBody.find("\"stream\":true"), std::string::npos);
}

TEST(DefaultLlmClient, SendWrapperWorksWithoutCallback) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{200,
      "data: {\"choices\":[{\"delta\":{\"content\":\"X\"}}]}\n\n"
      "data: [DONE]\n\n"};
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));
  Response r = client.send({userText("hi")}, {});
  EXPECT_EQ(assistantText(r.assistant_message), "X");
}

TEST(DefaultLlmClient, Non2xxThrowsLlmError) {
  auto transport = std::make_unique<FakeTransport>();
  transport->next = HttpResponse{500, "server error"};
  DefaultLlmClient client(testConfig(), std::make_unique<OpenAIProvider>(), std::move(transport));
  EXPECT_THROW(client.send({userText("hi")}, {}), LlmError);
}
```

> **注意：** 删掉了旧的 `BadJsonThrowsLlmError`（流式解析器对坏行容错、不抛），新增 `StreamSuccessAccumulatesResponse` 与 `SendWrapperWorksWithoutCallback`。

- [ ] **Step 4: 构建 + 测试（Task 3+4 一起）**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='DefaultLlmClient.*'`
Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 5: 提交**

```bash
git add src/llm/LlmClient.h src/llm/DefaultLlmClient.h src/llm/DefaultLlmClient.cpp tests/DefaultLlmClientTest.cpp
git commit -m "feat: sendStream primary + send() wrapper; DefaultLlmClient streams via parser"
```

---

## Task 5: AgentLoop 支持流式回调

**Files:**
- Modify: `src/core/AgentLoop.h`, `src/core/AgentLoop.cpp`
- Modify: `tests/AgentLoopTest.cpp`

- [ ] **Step 1: 修改 `src/core/AgentLoop.h`（run 加可选 onDelta）**

完整新内容：
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
  // onDelta（可选）：流式显示增量回调；不传则非流式语义（只在结束后拿完整结果）。
  // 基建失败时 client.sendStream 抛 LlmError，由调用方处理。
  std::string run(std::vector<Message>& messages,
                  const LlmClient::DeltaCallback& onDelta = {});
private:
  LlmClient& client_;
  const ToolRegistry& registry_;
  int maxIterations_;
};

}
```

- [ ] **Step 2: 修改 `src/core/AgentLoop.cpp`（改调 sendStream）**

完整新内容：
```cpp
#include "core/AgentLoop.h"

namespace aicoder {

AgentLoop::AgentLoop(LlmClient& client, const ToolRegistry& registry, int maxIterations)
    : client_(client), registry_(registry), maxIterations_(maxIterations) {}

std::string AgentLoop::run(std::vector<Message>& messages,
                           const LlmClient::DeltaCallback& onDelta) {
  // onDelta 可能为空（默认构造的 std::function）；包一层避免空调用
  LlmClient::DeltaCallback cb =
      onDelta ? onDelta : [](const StreamDelta&) {};

  for (int iter = 0; iter < maxIterations_; ++iter) {
    Response resp = client_.sendStream(messages, registry_.specs(), cb);
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

- [ ] **Step 3: 修改 `tests/AgentLoopTest.cpp`（FakeLlmClient 改 sendStream + 加 delta 测试）**

完整新内容：
```cpp
#include <gtest/gtest.h>
#include <functional>
#include "core/AgentLoop.h"
#include "core/ToolRegistry.h"
#include "core/Errors.h"
#include "llm/LlmClient.h"

using namespace aicoder;

namespace {
// 由测试脚本控制每次调用返回什么（按调用序号）；可选地吐出增量。
class FakeLlmClient : public LlmClient {
public:
  explicit FakeLlmClient(std::function<Response(int)> fn) : fn_(std::move(fn)) {}
  Response sendStream(const std::vector<Message>&, const std::vector<ToolSpec>&,
                      const DeltaCallback& onDelta) override {
    Response r = fn_(calls_++);
    // 把本轮文本作为单个增量吐出，便于测试流式转发
    std::string txt = assistantText(r.assistant_message);
    if (!txt.empty() && onDelta) onDelta(StreamDelta{txt, ""});
    return r;
  }
  int calls_ = 0;
private:
  std::function<Response(int)> fn_;
};

Response textResp(std::string t) {
  return Response{Message{Role::Assistant, {TextBlock{std::move(t)}}}, "", "stop"};
}
Response toolResp(std::string id, std::string name, json input) {
  return Response{Message{Role::Assistant,
                          {ToolUseBlock{std::move(id), std::move(name), std::move(input)}}},
                  "", "tool_calls"};
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
  ToolRegistry reg;
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
  std::string reply = loop.run(msgs);

  EXPECT_EQ(fake.calls_, 3);
  EXPECT_NE(reply.find("最大迭代"), std::string::npos);
}

TEST(AgentLoop, ForwardsStreamDeltaToCallback) {
  FakeLlmClient fake([](int) { return textResp("hello"); });
  ToolRegistry reg;
  AgentLoop loop(fake, reg, 16);
  std::vector<Message> msgs = {userText("hi")};
  std::string streamed;
  loop.run(msgs, [&](const StreamDelta& d) { streamed += d.text; });
  EXPECT_EQ(streamed, "hello");
}
```

> **注意：** `Response` 现在有三个字段 `{assistant_message, reasoning_content, finish_reason}`，所以 `textResp`/`toolResp` 的聚合初始化加了空 reasoning（中间的 `""`）。

- [ ] **Step 4: 构建 + 测试**

Run: `cmake --build build -j && ./build/aicoder_tests --gtest_filter='AgentLoop.*'`
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 5: 提交**

```bash
git add src/core/AgentLoop.h src/core/AgentLoop.cpp tests/AgentLoopTest.cpp
git commit -m "feat: AgentLoop.run accepts optional onDelta, calls sendStream"
```

---

## Task 6: ConsoleRepl 适配（保持非流式 UX）

**Files:**
- Modify: `tests/ConsoleReplTest.cpp`

> ConsoleRepl.cpp 不需要改：它调用 `loop_.run(messages_)`（不传 onDelta），AgentLoop 用 noop 回调，行为与之前一致（结束后打印完整回复）。只需修测试里的 FakeLlmClient。

- [ ] **Step 1: 修改 `tests/ConsoleReplTest.cpp`（FakeLlmClient 改 sendStream）**

完整新内容：
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
  Response sendStream(const std::vector<Message>&, const std::vector<ToolSpec>&,
                      const DeltaCallback&) override {
    return Response{Message{Role::Assistant, {TextBlock{reply_}}}, "", "stop"};
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

- [ ] **Step 2: 构建 + 全量测试**

Run: `cmake --build build -j && ctest --test-dir build 2>&1 | tail -3`
Expected: 全绿（约 46 tests：去掉 1 个旧 BadJson + 加 4 StreamParser + 1 stream client + 1 send wrapper + 1 agent delta + 1 transport stream，净增约 +6）。

- [ ] **Step 3: 提交**

```bash
git add tests/ConsoleReplTest.cpp
git commit -m "test: ConsoleRepl fake uses sendStream (behavior unchanged)"
```

---

## Task 7: TUI 流式气泡（ReplView + App）

**Files:**
- Modify: `src/ui/ReplView.h`, `src/ui/ReplView.cpp`, `src/ui/App.cpp`

- [ ] **Step 1: 修改 `src/ui/ReplView.h`（加 appendDelta）**

在 `setThinking` 声明后加：
```cpp
  // worker 线程调用：把流式增量追加到「进行中」的回复气泡（线程安全，经 Post）
  void appendDelta(const std::string& text, const std::string& reasoning);
```

完整新内容：
```cpp
#pragma once
#include <string>
#include <vector>
#include <functional>
#include "ftxui/component/component.hpp"

namespace aicoder {

struct UIMessage {
  std::string text;
  bool is_user = false;
  bool is_error = false;
};

class ReplView {
public:
  using SubmitCallback = std::function<void(const std::string& text)>;

  ReplView(SubmitCallback onSubmit);
  ftxui::Component component();

  // 完成一条消息（最终回复 / 用户输入 / 错误）
  void appendMessage(UIMessage msg);
  void appendError(const std::string& msg);

  // 流式增量：追加到进行中的气泡
  void appendDelta(const std::string& text, const std::string& reasoning);

  void setThinking(bool v);
  std::vector<UIMessage> messages() const;
  void setScreen(ftxui::ScreenInteractive* s);

private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}
```

- [ ] **Step 2: 修改 `src/ui/ReplView.cpp`（流式气泡状态 + 渲染）**

完整新内容：
```cpp
#include "ReplView.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include <thread>

namespace aicoder {

class ReplView::Impl {
public:
  ftxui::Component component;
  ftxui::Component input;
  ftxui::ScreenInteractive* screen = nullptr;
  std::vector<UIMessage> messages_;
  std::string input_text_;
  bool thinking_ = false;
  std::string stream_text_;       // 进行中回复的正文增量
  std::string stream_reasoning_;  // 进行中回复的思维链增量
  std::weak_ptr<Impl> self_;
  SubmitCallback onSubmit_;

  Impl() {
    ftxui::InputOption input_opt;
    input_opt.on_enter = [this] { onEnter(); };
    input = ftxui::Input(&input_text_, "输入消息，按 Enter 发送...", input_opt);

    auto message_renderer = ftxui::Renderer([this] {
      std::vector<ftxui::Element> els;
      for (const auto& m : messages_) {
        std::string prefix = m.is_user ? "❯ " : (m.is_error ? "✗ " : "◆ ");
        ftxui::Element line = ftxui::paragraph(prefix + m.text);
        if (m.is_error) line = ftxui::dim(std::move(line));
        els.push_back(std::move(line));
      }
      // 进行中的流式气泡
      if (thinking_) {
        if (!stream_reasoning_.empty())
          els.push_back(ftxui::paragraph("  " + stream_reasoning_) | ftxui::dim);
        std::string body = stream_text_.empty() ? "[思考中...]" : stream_text_;
        els.push_back(ftxui::paragraph("◆ " + body));
      }
      return ftxui::vbox(std::move(els)) | ftxui::flex;
    });

    component = ftxui::Container::Vertical({
      message_renderer | ftxui::flex | ftxui::yframe | ftxui::vscroll_indicator,
      input
    });
  }

  void onEnter() {
    if (thinking_) return;
    std::string txt = input_text_;
    if (txt.empty()) return;
    input_text_.clear();
    if (onSubmit_) onSubmit_(txt);
  }
};

ReplView::ReplView(SubmitCallback onSubmit) : impl_(std::make_shared<Impl>()) {
  impl_->self_ = impl_;
  impl_->onSubmit_ = std::move(onSubmit);
}

ftxui::Component ReplView::component() { return impl_->component; }

void ReplView::appendMessage(UIMessage msg) {
  if (!impl_->screen) { impl_->messages_.push_back(std::move(msg)); return; }
  auto self = impl_->self_;
  impl_->screen->Post([self, msg = std::move(msg)]() {
    auto p = self.lock();
    if (!p) return;
    p->messages_.push_back(std::move(msg));
    p->stream_text_.clear();
    p->stream_reasoning_.clear();
    p->thinking_ = false;
  });
}

void ReplView::appendError(const std::string& msg) {
  appendMessage({msg, false, true});
}

void ReplView::appendDelta(const std::string& text, const std::string& reasoning) {
  if (!impl_->screen) return;
  auto self = impl_->self_;
  impl_->screen->Post([self, text, reasoning]() {
    auto p = self.lock();
    if (!p) return;
    p->stream_text_ += text;
    p->stream_reasoning_ += reasoning;
  });
}

void ReplView::setThinking(bool v) {
  if (!impl_->screen) { impl_->thinking_ = v; return; }
  auto self = impl_->self_;
  impl_->screen->Post([self, v]() {
    auto p = self.lock();
    if (p) p->thinking_ = v;
  });
}

std::vector<UIMessage> ReplView::messages() const { return impl_->messages_; }

void ReplView::setScreen(ftxui::ScreenInteractive* s) { impl_->screen = s; }

}
```

> **说明：** 用户消息（`is_user`）也走 `appendMessage` → 会清掉 stream 状态。提交时序：App 先 `appendMessage(用户输入)`，再 `setThinking(true)`，worker 流式 `appendDelta`，最后 `appendMessage(最终回复)`。见 Task 7 Step 3 调整后的 App 时序。

- [ ] **Step 3: 修改 `src/ui/App.cpp`（onSubmit 传 onDelta + 调整时序）**

把 `App::Impl::onSubmit` 替换为：
```cpp
  void onSubmit(const std::string& input) {
    auto cr = router.handle(input, messages);
    if (cr == CommandResult::Quit) { screen.Exit(); return; }
    if (cr == CommandResult::Cleared) {
      replView.appendMessage({"[对话已清空]", false, false});
      return;
    }
    messages.push_back(userText(input));
    replView.appendMessage({input, true, false});  // 先落用户气泡（清掉旧 stream 状态）
    replView.setThinking(true);

    auto loopPtr = &agentLoop;
    auto msgsPtr = &messages;
    auto rv = &replView;
    auto scr = &screen;
    std::thread([=]() {
      try {
        std::string reply = loopPtr->run(*msgsPtr, [rv](const StreamDelta& d) {
          rv->appendDelta(d.text, d.reasoning);   // worker 线程 → 内部再 Post
        });
        rv->appendMessage({reply, false, false});  // 终态：清 stream、关 thinking
      } catch (const LlmError& e) {
        rv->appendError(std::string("[错误] ") + e.what());
      }
    }).detach();
  }
```

确保 `App.cpp` 顶部 include 了 `llm/StreamDelta.h`（经 `LlmClient.h` 间接也可，但显式更稳）：
```cpp
#include "llm/StreamDelta.h"
```

> **线程说明：** `loop.run` 在 worker 线程执行；它的 onDelta 在 worker 线程被调用，`appendDelta` 内部用 `screen->Post` 投递到 UI 线程，安全。`appendMessage`/`appendError` 同理。终态的 `appendMessage` 会清掉流式气泡并把完整回复落为正式消息——避免「流式气泡」和「最终消息」重复显示（流式期间显示 stream_text_，结束时清空并 push 完整 reply）。

- [ ] **Step 4: 构建**

Run: `cmake --build build -j 2>&1 | grep -E "error:" | head; cmake --build build --target aicoder_tui 2>&1 | tail -2`
Expected: 无 error，`aicoder_tui` 构建成功。

- [ ] **Step 5: 全量测试（确认内核测试仍绿）**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: 全绿。

- [ ] **Step 6: 提交**

```bash
git add src/ui/ReplView.h src/ui/ReplView.cpp src/ui/App.cpp
git commit -m "feat: TUI streaming bubble (reasoning dim + content live via appendDelta)"
```

---

## Task 8: 人工验收（真实 API，TUI 流式）

- [ ] **Step 1: 构建 + key**

```bash
cmake --build build -j
export AICODER_API_KEY=<你的 key>
```

- [ ] **Step 2: TUI 流式对话**

```bash
./build/aicoder_tui
> 写一段 200 字的自我介绍
```
Expected: 回复**逐字/逐块**出现，而非一次性蹦出。reasoner 模型先显示暗色思维链增量，再显示正文。

- [ ] **Step 3: TUI 流式 + 工具**

```bash
> 读一下 ./CMakeLists.txt 并总结
```
Expected: 思考流式 → 工具执行（短暂）→ 总结流式输出。

- [ ] **Step 4: 控制台仍非流式（对照）**

```bash
./build/aicoder
> 写一段 200 字的自我介绍
```
Expected: 回复一次性出现（收完再打印），行为同改造前。

- [ ] **Step 5: 错误流式路径**

TUI 里输入触发工具失败（读不存在文件）→ 错误以红行显示，思考状态退出。

- [ ] **Step 6: 记录验收**

```bash
git commit --allow-empty -m "test: streaming TUI acceptance passed (manual)"
```

---

## Self-Review

**Spec coverage:**
- SSE 解析（跨 chunk / content / reasoning / 分片 tool_calls）→ Task 1 ✓
- Transport.postStream + libcurl 流式 write callback → Task 2 ✓
- sendStream 主方法 + send 包装 → Task 3 ✓
- DefaultLlmClient 流式（stream:true + parser）→ Task 4 ✓
- AgentLoop onDelta + 调 sendStream → Task 5 ✓
- 控制台保持非流式 UX → Task 6（不改 ConsoleRepl.cpp，仅 noop 回调）✓
- TUI 流式气泡（reasoning 暗色 + content 实时）→ Task 7 ✓
- 「只做 TUI 流式」：console 走 noop，TUI 走真回调 ✓
- 工具阶段非流式：AgentLoop 在工具执行期间无增量 ✓
- 人工验收 → Task 8 ✓

**类型一致性：**
- `Response{assistant_message, reasoning_content, finish_reason}` —— 三字段聚合初始化在 AgentLoopTest 的 `textResp`/`toolResp` 已更新（中间补 `""`）。StreamParser.finish() 和 DefaultLlmClient 都填这三个字段。
- `LlmClient::DeltaCallback = std::function<void(const StreamDelta&)>`，`sendStream(messages, tools, onDelta)` 纯虚；`send(messages, tools)` 非虚包装。所有 fake（AgentLoopTest / ConsoleReplTest）override `sendStream`；DefaultLlmClientTest 的 FakeTransport override `postStream`。
- `OpenAIStreamParser::feed(chunk, onDelta)` + `finish()`；`Transport::ChunkCallback`；`AgentLoop::run(messages, onDelta={})`。
- `OpenAIProvider`（encodeRequest/decodeResponse）**未改**，其 8 个单测保持。

**关键风险点（实现者注意）：**
1. `Response` 现在三字段——任何用聚合初始化 `Response{msg, "reason"}` 的旧代码会编译错；必须写成 `Response{msg, "", "reason"}` 或具名赋值。
2. `send()` 从 virtual override 变为基类非虚包装——所有 override `send` 的 fake 必须改 override `sendStream`，否则编译错。
3. TUI 中 onDelta 在 worker 线程触发，必须经 `screen->Post`（已在 ReplView::appendDelta 内处理）。
4. 流式期间显示 `stream_text_`，终态 `appendMessage` 清空它并 push 完整 reply——注意不要两份都显示。