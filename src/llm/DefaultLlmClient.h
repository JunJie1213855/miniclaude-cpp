#pragma once
#include <atomic>
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
  // 公共流式接口(基类 LlmClient 已有,这里 override)。
  Response sendStream(const std::vector<Message>& messages,
                      const std::vector<ToolSpec>& tools,
                      const DeltaCallback& onDelta) override;
  // 扩展:支持 cancel 信号(非 nullptr 时由 transport 检查并中断 HTTP)。
  // 旧调用走基类版本(cancel=nullptr),新调用方显式传 cancel token。
  Response sendStream(const std::vector<Message>& messages,
                      const std::vector<ToolSpec>& tools,
                      const DeltaCallback& onDelta,
                      const std::atomic<bool>* cancel);
private:
  Response doSendStream(const std::vector<Message>& messages,
                        const std::vector<ToolSpec>& tools,
                        const DeltaCallback& onDelta,
                        const std::atomic<bool>* cancel);
  Config config_;
  std::unique_ptr<Provider> provider_;
  std::unique_ptr<Transport> transport_;
};
}
