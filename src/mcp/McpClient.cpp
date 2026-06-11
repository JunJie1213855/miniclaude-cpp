#include "McpClient.h"
#include "StdioTransport.h"

#include "core/Json.h"
#include "util/Log.h"

namespace aicoder::mcp {

McpClient::McpClient(McpServerConfig config)
	: config_(std::move(config))
{
}

McpClient::~McpClient()
{
	disconnect();
}

bool McpClient::connect()
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (state_ == State::Ready) return true;

	transport_ = std::make_unique<StdioTransport>(
		config_.command, config_.args, config_.env);
	if (cancel_) transport_->setCancelToken(cancel_);

	if (!transport_->start()) {
		state_ = State::Error;
		AICODER_LOG_ERROR("McpClient: failed to start transport for '{}'",
		                  config_.name);
		return false;
	}

	state_ = State::Initializing;

	// Send initialize request
	json initReq = {
		{"jsonrpc", "2.0"},
		{"id", next_id_++},
		{"method", "initialize"},
		{"params", {
			{"protocolVersion", "2024-11-05"},
			{"capabilities", json::object()},
			{"clientInfo", {
				{"name", "aicoder"},
				{"version", "1.0"}
			}}
		}}
	};
	transport_->writeLine(initReq.dump());

	std::string line = readResponse();
	auto resp = deserializeResponse(line);
	if (resp.is_error) {
		AICODER_LOG_ERROR("McpClient: initialize failed for '{}': {}",
		                  config_.name,
		                  resp.error.value("message", "unknown error"));
		state_ = State::Error;
		return false;
	}

	server_name_ = resp.result.value("serverInfo",
	                                  json::object()).value("name", "unknown");
	server_version_ = resp.result.value("serverInfo",
	                                    json::object()).value("version", "0");

	AICODER_LOG_INFO("McpClient: connected to '{}' ({} v{})",
	                 config_.name, server_name_, server_version_);

	// Send initialized notification
	json notify = {
		{"jsonrpc", "2.0"},
		{"method", "notifications/initialized"}
	};
	transport_->writeLine(notify.dump());

	state_ = State::Ready;
	return true;
}

void McpClient::disconnect()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (transport_) {
		transport_->shutdown();
		transport_.reset();
	}
	state_ = State::Disconnected;
}

bool McpClient::isReady() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return state_ == State::Ready;
}

std::vector<McpToolDef> McpClient::listTools()
{
	auto result = sendRequest("tools/list", json::object());

	std::vector<McpToolDef> out;
	for (const auto& tool : result["tools"]) {
		McpToolDef t;
		t.name = tool["name"];
		t.description = tool.value("description", "");
		t.input_schema = tool.value("inputSchema", json::object());
		out.push_back(std::move(t));
	}
	return out;
}

std::string McpClient::callTool(const std::string& name,
                                const json& arguments)
{
	json params = {
		{"name", name},
		{"arguments", arguments}
	};
	auto result = sendRequest("tools/call", params);

	std::string text;
	for (const auto& c : result["content"]) {
		if (c.value("type", "") == "text") {
			text += c.value("text", "");
		}
	}
	return text;
}

void McpClient::setCancelToken(std::shared_ptr<std::atomic<bool>> cancel)
{
	std::lock_guard<std::mutex> lock(mutex_);
	cancel_ = std::move(cancel);
	if (transport_) transport_->setCancelToken(cancel_);
}

json McpClient::sendRequest(const std::string& method, const json& params)
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (state_ != State::Ready) {
		throw McpError("McpClient: not connected");
	}

	int64_t id = next_id_++;
	json j = {
		{"jsonrpc", "2.0"},
		{"id", id},
		{"method", method},
		{"params", params}
	};
	transport_->writeLine(j.dump());

	std::string line = readResponse();
	auto resp = deserializeResponse(line);

	if (resp.id != id) {
		throw McpError("McpClient: response id mismatch");
	}
	if (resp.is_error) {
		throw McpError(
			resp.error.value("message", "unknown error"));
	}

	return resp.result;
}

std::string McpClient::readResponse()
{
	while (true) {
		std::string line = transport_->readLine();
		if (line.empty()) {
			throw McpError("McpClient: transport closed unexpectedly");
		}

		try {
			json j = json::parse(line);
			if (!j.contains("id")) {
				// Notification — skip
				continue;
			}
		} catch (const json::parse_error&) {
			throw McpError("McpClient: invalid JSON: " +
			               line.substr(0, 100));
		}

		return line;
	}
}

std::string McpClient::serializeRequest(const JsonRpcRequest& req)
{
	json j = {
		{"jsonrpc", req.jsonrpc},
		{"id", req.id},
		{"method", req.method},
		{"params", req.params}
	};
	return j.dump();
}

JsonRpcResponse McpClient::deserializeResponse(const std::string& line)
{
	json j = json::parse(line);
	JsonRpcResponse resp;
	resp.jsonrpc = j.value("jsonrpc", "");
	resp.id = j.value("id", int64_t(0));
	if (j.contains("result")) {
		resp.result = j["result"];
	}
	if (j.contains("error")) {
		resp.error = j["error"];
		resp.is_error = true;
	}
	return resp;
}

}  // namespace aicoder::mcp
