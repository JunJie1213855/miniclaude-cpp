#pragma once
#include <optional>
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"

namespace aicoder {

struct SessionData {
  std::string id;
  std::string created_at;          // ISO-8601 local time
  std::string updated_at;          // ISO-8601 local time
  std::string model;               // informational; LLM uses current config on resume
  std::vector<Message> messages;   // excludes System (rebuilt on resume)
};

// Serialize a SessionData to its on-disk JSON shape (schema_version=1).
json sessionToJson(const SessionData& s);
// Parse a JSON object back into SessionData; returns nullopt on bad schema/version/structure.
std::optional<SessionData> sessionFromJson(const json& j);

}  // namespace aicoder
