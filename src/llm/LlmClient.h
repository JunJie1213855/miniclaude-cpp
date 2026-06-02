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
  using PermissionCallback = std::function<bool(const std::string& toolName, const json& input)>;

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
