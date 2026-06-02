#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace aicoder
{
  struct HttpResponse
  {
    long status = 0;
    std::string body;
  };
  class Transport
  {
  public:
    virtual ~Transport() = default;

    // 非流式：一次性 POST，收完整 body。
    // cancel（非 nullptr）:传输层每 200ms 检查一次,若置 true → 立刻
    // 终止底层 HTTP 连接(curl progress callback 返回非 0),抛
    // LlmError("aborted by callback")。默认 nullptr = 老调用零侵入。
    virtual HttpResponse post(const std::string &url,
                              const std::string &body,
                              const std::vector<std::string> &headers,
                              const std::atomic<bool> *cancel = nullptr) = 0;

    // 流式：每收到一块响应字节就调用 onChunk(原始字节)。
    // 仍返回 HttpResponse（status + 累积的完整 body，供非 2xx 错误报告用）。
    // 传输层失败（连接/超时/URL 错误）抛 LlmError。
    // cancel 语义同 post。
    using ChunkCallback = std::function<void(const std::string &chunk)>;
    virtual HttpResponse postStream(const std::string &url,
                                    const std::string &body,
                                    const std::vector<std::string> &headers,
                                    const ChunkCallback &onChunk,
                                    const std::atomic<bool> *cancel = nullptr) = 0;
  };
}
