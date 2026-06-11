#pragma once

#include <memory>
#include <string>

#include "core/Tool.h"
#include "mcp/McpTypes.h"

namespace aicoder::mcp {

class McpClient;

/// Build the qualified tool name: mcp__<server>__<tool>
std::string qualifiedName(const std::string& serverName,
                          const std::string& toolName);

/// Wrap an MCP tool definition into a Tool that delegates execution to the
/// McpClient. The returned Tool has needsPermission = true and converts
/// McpError into ToolError.
Tool makeToolFromMcp(const McpToolDef& def,
                     std::shared_ptr<McpClient> client,
                     const std::string& serverName);

}  // namespace aicoder::mcp
