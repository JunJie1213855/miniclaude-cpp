#include <gtest/gtest.h>
#include "llm/HttpTransport.h"
#include "core/Errors.h"

using namespace aicoder;

// 不支持的协议 → curl 在解析阶段就失败（CURLE_UNSUPPORTED_PROTOCOL）→ LlmError。
// 解析期失败先于任何代理/网络，因此与环境中的 HTTP(S)_PROXY 无关，确定性。
// 注意：不能用 "not-a-valid-url" —— libcurl 会补成 http://not-a-valid-url，
// 若环境设了代理则会被路由到代理并拿到 502（CURLE_OK），不会抛异常。
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
