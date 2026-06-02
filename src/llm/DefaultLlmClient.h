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
