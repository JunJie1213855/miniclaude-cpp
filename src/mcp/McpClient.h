#pragma once
#include "McpTypes.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace aicoder::mcp {

class StdioTransport;

class McpClient {
public:
	explicit McpClient(McpServerConfig config);
	~McpClient();

	McpClient(const McpClient&) = delete;
	McpClient& operator=(const McpClient&) = delete;

	bool connect();
	void disconnect();
	bool isReady() const;
	std::vector<McpToolDef> listTools();
	std::string callTool(const std::string& name, const json& arguments);
	void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel);
	const std::string& serverName() const { return config_.name; }

private:
	json sendRequest(const std::string& method, const json& params);
	std::string readResponse();
	static std::string serializeRequest(const JsonRpcRequest& req);
	static JsonRpcResponse deserializeResponse(const std::string& line);

	enum class State { Disconnected, Initializing, Ready, Error };

	McpServerConfig config_;
	std::unique_ptr<StdioTransport> transport_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	State state_ = State::Disconnected;
	int64_t next_id_ = 1;
	mutable std::mutex mutex_;
	std::string server_name_;
	std::string server_version_;
};

}  // namespace aicoder::mcp
