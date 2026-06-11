#include "McpToolAdapter.h"
#include "McpClient.h"

#include "core/Errors.h"

namespace aicoder::mcp {

std::string qualifiedName(const std::string& serverName,
                          const std::string& toolName)
{
    return "mcp__" + serverName + "__" + toolName;
}

Tool makeToolFromMcp(const McpToolDef& def,
                     std::shared_ptr<McpClient> client,
                     const std::string& serverName)
{
    Tool t;
    t.name = qualifiedName(serverName, def.name);
    t.description = def.description;
    t.input_schema = def.input_schema;
    t.needsPermission = true;
    t.execute = [client, toolName = def.name](const json& input) -> std::string {
        try {
            return client->callTool(toolName, input);
        } catch (const McpError& e) {
            throw ToolError(std::string("MCP: ") + e.what());
        }
    };
    return t;
}

}  // namespace aicoder::mcp
