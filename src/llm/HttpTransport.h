#pragma once
#include "llm/Transport.h"

namespace aicoder {
class HttpTransport : public Transport {
public:
  HttpTransport();   // 首次构造时一次性 curl_global_init
  HttpResponse post(const std::string& url,
                    const std::string& body,
                    const std::vector<std::string>& headers) override;
  HttpResponse postStream(const std::string& url,
                          const std::string& body,
                          const std::vector<std::string>& headers,
                          const ChunkCallback& onChunk) override;
};
}
