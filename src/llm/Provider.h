#pragma once
#include <string>
#include <vector>
#include "core/Json.h"
#include "core/Message.h"
#include "core/Tool.h"
#include "llm/Response.h"

namespace aicoder
{
  class Provider
  {
  public:
    virtual ~Provider() = default;
    virtual json encodeRequest(const std::vector<Message> &messages,
                               const std::vector<ToolSpec> &tools,
                               const std::string &model) const = 0;
    virtual Response decodeResponse(const json &body) const = 0;
  };
}
