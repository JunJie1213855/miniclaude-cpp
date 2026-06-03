#pragma once
#include <string>
namespace aicoder {
class LlmClient;
struct GeneratedResource {
  std::string name;          // resource name (e.g. "greet")
  std::string description;   // one-line description
  std::string body;          // markdown body / system prompt
};
// Synchronously call main LLM to generate a resource. The kind determines
// the system prompt guiding generation. The userHint is the user-provided
// description (e.g. "/create_skill greet" or "/create_skill greet 中文回复").
GeneratedResource generateResource(LlmClient& client, const std::string& kind, const std::string& userHint, const std::string& desiredName);
}
