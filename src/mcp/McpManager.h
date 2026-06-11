#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mcp/McpTypes.h"

namespace aicoder {

class ToolRegistry;

namespace mcp {

class McpClient;

/// Manages the lifecycle of multiple MCP server connections and registers their
/// tools into a ToolRegistry.
class McpManager
{
public:
    explicit McpManager(std::vector<McpServerConfig> configs);
    ~McpManager();

    McpManager(const McpManager&) = delete;
    McpManager& operator=(const McpManager&) = delete;

    /// Connect all configured servers, list their tools, and register every
    /// tool into the given registry.  On the first call servers are connected;
    /// subsequent calls re-list and re-register tools from already-connected
    /// servers (skipping failed ones).  Returns the total number of tools
    /// registered.
    int registerAllTools(ToolRegistry& registry);

    /// Return the prefix strings for each server ("mcp__<name>__").
    std::vector<std::string> serverToolPrefixes() const;

    /// Return every fully-qualified tool name registered across all servers.
    std::vector<std::string> allToolNames() const;

    /// Disconnect all servers and mark as uninitialised.
    void shutdownAll();

    /// Propagate a cancel token to every connected client.
    void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel);

    /// True if the named server was configured but failed to connect / list.
    bool serverFailed(const std::string& name) const;

private:
    struct ServerEntry
    {
        McpServerConfig config;
        std::shared_ptr<McpClient> client;
        std::vector<std::string> tool_names;
        bool failed = false;
    };

    void connectOne(ServerEntry& entry);

    std::vector<ServerEntry> servers_;
    bool initialized_ = false;
    std::shared_ptr<std::atomic<bool>> cancel_;
    mutable std::mutex mutex_;
};

}  // namespace mcp
}  // namespace aicoder
