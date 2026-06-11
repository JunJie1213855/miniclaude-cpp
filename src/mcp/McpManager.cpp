#include "McpManager.h"
#include "McpClient.h"
#include "McpToolAdapter.h"

#include "core/ToolRegistry.h"
#include "util/Log.h"

namespace aicoder::mcp {

McpManager::McpManager(std::vector<McpServerConfig> configs)
{
    servers_.reserve(configs.size());
    for (auto& cfg : configs) {
        ServerEntry entry;
        entry.config = std::move(cfg);
        servers_.push_back(std::move(entry));
    }
}

McpManager::~McpManager()
{
    shutdownAll();
}

int McpManager::registerAllTools(ToolRegistry& registry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;

    if (initialized_) {
        // Re-registration: re-list tools from each non-failed server.
        for (auto& entry : servers_) {
            if (entry.failed) continue;
            try {
                auto defs = entry.client->listTools();
                entry.tool_names.clear();
                for (const auto& def : defs) {
                    registry.registerTool(
                        makeToolFromMcp(def, entry.client, entry.config.name));
                    entry.tool_names.push_back(
                        qualifiedName(entry.config.name, def.name));
                    ++count;
                }
            } catch (const McpError& e) {
                AICODER_LOG_ERROR(
                    "McpManager: re-register failed for '{}': {}",
                    entry.config.name, e.what());
                entry.failed = true;
            }
        }
        return count;
    }

    // First call: connect every server and register its tools.
    for (auto& entry : servers_) {
        connectOne(entry);
        if (entry.failed) continue;

        try {
            auto defs = entry.client->listTools();
            for (const auto& def : defs) {
                registry.registerTool(
                    makeToolFromMcp(def, entry.client, entry.config.name));
                entry.tool_names.push_back(
                    qualifiedName(entry.config.name, def.name));
                ++count;
            }
            AICODER_LOG_INFO("McpManager: registered {} tools from '{}'",
                             entry.tool_names.size(), entry.config.name);
        } catch (const McpError& e) {
            AICODER_LOG_ERROR(
                "McpManager: failed to list tools for '{}': {}",
                entry.config.name, e.what());
            entry.failed = true;
        }
    }

    initialized_ = true;
    return count;
}

void McpManager::connectOne(ServerEntry& entry)
{
    entry.client = std::make_shared<McpClient>(entry.config);
    if (cancel_) {
        entry.client->setCancelToken(cancel_);
    }
    if (!entry.client->connect()) {
        AICODER_LOG_ERROR("McpManager: failed to connect to '{}'",
                          entry.config.name);
        entry.failed = true;
    }
}

std::vector<std::string> McpManager::serverToolPrefixes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> prefixes;
    prefixes.reserve(servers_.size());
    for (const auto& entry : servers_) {
        prefixes.push_back("mcp__" + entry.config.name + "__");
    }
    return prefixes;
}

std::vector<std::string> McpManager::allToolNames() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& entry : servers_) {
        names.insert(names.end(),
                     entry.tool_names.begin(),
                     entry.tool_names.end());
    }
    return names;
}

void McpManager::shutdownAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : servers_) {
        if (entry.client) {
            entry.client->disconnect();
        }
    }
    initialized_ = false;
}

bool McpManager::serverFailed(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : servers_) {
        if (entry.config.name == name) {
            return entry.failed;
        }
    }
    return false;
}

void McpManager::setCancelToken(std::shared_ptr<std::atomic<bool>> cancel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_ = cancel;
    for (auto& entry : servers_) {
        if (entry.client) {
            entry.client->setCancelToken(cancel);
        }
    }
}

}  // namespace aicoder::mcp
