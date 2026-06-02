#include "llm/DefaultLlmClient.h"
#include "llm/StreamParser.h"
#include "core/Errors.h"

namespace aicoder
{

  DefaultLlmClient::DefaultLlmClient(Config config,
                                     std::unique_ptr<Provider> provider,
                                     std::unique_ptr<Transport> transport)
      : config_(std::move(config)),
        provider_(std::move(provider)),
        transport_(std::move(transport)) {}

  Response DefaultLlmClient::sendStream(const std::vector<Message> &messages,
                                        const std::vector<ToolSpec> &tools,
                                        const DeltaCallback &onDelta)
  {
    json reqBody = provider_->encodeRequest(messages, tools, config_.model);
    reqBody["stream"] = true;
    std::string url = config_.base_url + "/chat/completions";
    std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + config_.api_key};

    OpenAIStreamParser parser;
    HttpResponse resp = transport_->postStream(
        url, reqBody.dump(), headers,
        [&](const std::string &chunk)
        {
          parser.feed(chunk, onDelta);
        });

    if (resp.status < 200 || resp.status >= 300)
      throw LlmError("HTTP " + std::to_string(resp.status) + ": " + resp.body);

    return parser.finish();
  }

}
