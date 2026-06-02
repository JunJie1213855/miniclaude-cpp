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
