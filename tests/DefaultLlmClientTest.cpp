#include <gtest/gtest.h>
#include <atomic>
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
                    const std::vector<std::string>&,
                    const std::atomic<bool>* /*cancel*/ = nullptr) override {
    lastBody = body;
    return next;
  }
  HttpResponse postStream(const std::string&, const std::string& body,
                          const std::vector<std::string>&,
                          const ChunkCallback& onChunk,
                          const std::atomic<bool>* /*cancel*/ = nullptr) override {
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
