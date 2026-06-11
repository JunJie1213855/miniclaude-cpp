#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <sys/types.h>

namespace aicoder::mcp {

class StdioTransport {
public:
	StdioTransport(std::string command,
	               std::vector<std::string> args = {},
	               std::map<std::string, std::string> env = {});
	~StdioTransport();

	StdioTransport(const StdioTransport&) = delete;
	StdioTransport& operator=(const StdioTransport&) = delete;

	bool start();
	void writeLine(const std::string& line);
	std::string readLine();
	bool isRunning() const;
	void shutdown(int timeoutSec = 5);
	pid_t pid() const { return pid_; }

	void setCancelToken(std::shared_ptr<std::atomic<bool>> cancel) {
		cancel_ = std::move(cancel);
	}

private:
	void closePipes();

	std::string command_;
	std::vector<std::string> args_;
	std::map<std::string, std::string> env_;
	pid_t pid_ = 0;
	int stdin_fd_ = -1;
	int stdout_fd_ = -1;
	std::shared_ptr<std::atomic<bool>> cancel_;
};

}  // namespace aicoder::mcp
