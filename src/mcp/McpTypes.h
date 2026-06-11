#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Json.h"

namespace aicoder::mcp {

struct JsonRpcRequest {
	std::string jsonrpc = "2.0";
	int64_t id = 0;
	std::string method;
	json params;
};

struct JsonRpcResponse {
	std::string jsonrpc;
	int64_t id = 0;
	json result;
	json error;
	bool is_error = false;
};

struct McpToolDef {
	std::string name;
	std::string description;
	json input_schema;
};

struct McpServerConfig {
	std::string name;
	std::string command;
	std::vector<std::string> args;
	std::map<std::string, std::string> env;
	std::string description;
};

struct McpError : std::runtime_error {
	using std::runtime_error::runtime_error;
};

}  // namespace aicoder::mcp
