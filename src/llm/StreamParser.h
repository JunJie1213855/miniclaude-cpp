#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "llm/Response.h"
#include "llm/StreamDelta.h"

namespace aicoder
{

  // OpenAI/DeepSeek 兼容的 SSE 流式解析器。
  // feed() 可被多次调用，每次喂入任意一段原始 SSE 字节（可能含多条/半条 data 行）。
  // 解析出的可显示增量通过 onDelta 实时吐出；同时内部累积完整消息，
  // 流结束后由 finish() 产出与非流式 decodeResponse 等价的 Response。
  class OpenAIStreamParser
  {
  public:
    using DeltaFn = std::function<void(const StreamDelta &)>;

    void feed(const std::string &chunk, const DeltaFn &onDelta);
    Response finish() const;

  private:
    void processLine(const std::string &line, const DeltaFn &onDelta);

    std::string buffer_; // 跨 chunk 残留的半行
    std::string content_;
    std::string reasoning_;
    std::string finish_reason_;

    struct ToolAccum
    {
      std::string id, name, args;
    };
    std::vector<ToolAccum> tools_; // 按 index 累积
  };

}
