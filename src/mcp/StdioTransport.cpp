#include "mcp/StdioTransport.h"
#include "mcp/McpTypes.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/Log.h"

namespace aicoder::mcp {

StdioTransport::StdioTransport(std::string command,
                               std::vector<std::string> args,
                               std::map<std::string, std::string> env)
    : command_(std::move(command)),
      args_(std::move(args)),
      env_(std::move(env)) {}

StdioTransport::~StdioTransport() {
	if (isRunning()) {
		shutdown();
	}
	closePipes();
}

bool StdioTransport::start() {
	int stdin_pipe[2];
	int stdout_pipe[2];

	if (pipe(stdin_pipe) != 0) {
		AICODER_LOG_ERROR("StdioTransport: pipe() for stdin failed: {}",
		                  std::strerror(errno));
		return false;
	}
	if (pipe(stdout_pipe) != 0) {
		AICODER_LOG_ERROR("StdioTransport: pipe() for stdout failed: {}",
		                  std::strerror(errno));
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return false;
	}

	pid_ = fork();
	if (pid_ < 0) {
		AICODER_LOG_ERROR("StdioTransport: fork() failed: {}",
		                  std::strerror(errno));
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return false;
	}

	if (pid_ == 0) {
		// Child process
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);

		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);

		// Set environment variables
		for (const auto& [key, value] : env_) {
			setenv(key.c_str(), value.c_str(), 1);
		}

		// Build argv
		std::vector<char*> argv;
		argv.reserve(args_.size() + 2);
		argv.push_back(const_cast<char*>(command_.c_str()));
		for (auto& arg : args_) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(command_.c_str(), argv.data());
		// execvp only returns on error
		AICODER_LOG_ERROR("StdioTransport: execvp({}) failed: {}",
		                  command_, std::strerror(errno));
		_exit(127);
	}

	// Parent process
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	stdin_fd_ = stdin_pipe[1];
	stdout_fd_ = stdout_pipe[0];

	// Set stdout_fd_ to non-blocking for readLine polling
	int flags = fcntl(stdout_fd_, F_GETFL, 0);
	if (flags == -1) {
		AICODER_LOG_ERROR("StdioTransport: fcntl(F_GETFL) failed: {}",
		                  std::strerror(errno));
		closePipes();
		return false;
	}
	if (fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
		AICODER_LOG_ERROR("StdioTransport: fcntl(F_SETFL O_NONBLOCK) failed: {}",
		                  std::strerror(errno));
		closePipes();
		return false;
	}

	AICODER_LOG_INFO("StdioTransport: started pid={} command={}", pid_, command_);
	return true;
}

void StdioTransport::writeLine(const std::string& line) {
	if (stdin_fd_ < 0) {
		throw McpError("StdioTransport: stdin pipe not open");
	}

	std::string data = line + "\n";
	const char* buf = data.data();
	size_t remaining = data.size();

	while (remaining > 0) {
		ssize_t written = write(stdin_fd_, buf, remaining);
		if (written < 0) {
			if (errno == EAGAIN) {
				continue;
			}
			if (errno == EPIPE) {
				throw McpError("StdioTransport: child process closed stdin pipe");
			}
			throw McpError(std::string("StdioTransport: write failed: ") +
			               std::strerror(errno));
		}
		buf += written;
		remaining -= static_cast<size_t>(written);
	}
}

std::string StdioTransport::readLine() {
	if (stdout_fd_ < 0) {
		throw McpError("StdioTransport: stdout pipe not open");
	}

	std::string line;
	char ch;

	while (true) {
		// Check cancel token
		if (cancel_ && cancel_->load(std::memory_order_relaxed)) {
			throw McpError("StdioTransport: read cancelled");
		}

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(stdout_fd_, &fds);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100000;  // 100ms

		int ret = select(stdout_fd_ + 1, &fds, nullptr, nullptr, &tv);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			throw McpError(std::string("StdioTransport: select failed: ") +
			               std::strerror(errno));
		}

		if (ret == 0) {
			// Timeout – loop again (check cancel)
			continue;
		}

		while (true) {
			ssize_t n = read(stdout_fd_, &ch, 1);
			if (n < 0) {
				if (errno == EAGAIN) {
					break;  // No more data right now, back to select
				}
				throw McpError(std::string("StdioTransport: read failed: ") +
				               std::strerror(errno));
			}
			if (n == 0) {
				// EOF — child exited
				throw McpError("StdioTransport: child process closed stdout");
			}
			if (ch == '\n') {
				return line;
			}
			line += ch;
		}
	}
}

bool StdioTransport::isRunning() const {
	if (pid_ <= 0) {
		return false;
	}
	return kill(pid_, 0) == 0;
}

void StdioTransport::shutdown(int timeoutSec) {
	if (pid_ <= 0) {
		return;
	}

	AICODER_LOG_INFO("StdioTransport: shutting down pid={} timeout={}s",
	                 pid_, timeoutSec);

	// Send SIGTERM
	kill(pid_, SIGTERM);

	// Wait up to timeoutSec for graceful exit
	for (int i = 0; i < timeoutSec * 10; ++i) {
		int status = 0;
		pid_t result = waitpid(pid_, &status, WNOHANG);
		if (result == pid_) {
			AICODER_LOG_INFO("StdioTransport: pid={} exited with status={}",
			                 pid_, WEXITSTATUS(status));
			pid_ = 0;
			return;
		}
		if (result < 0) {
			// Error (e.g. already reaped)
			pid_ = 0;
			return;
		}
		// Still running – poll every 100ms
		usleep(100000);
	}

	// Force kill
	AICODER_LOG_WARN("StdioTransport: pid={} did not exit, sending SIGKILL",
	                 pid_);
	kill(pid_, SIGKILL);
	waitpid(pid_, nullptr, 0);
	pid_ = 0;
}

void StdioTransport::closePipes() {
	if (stdin_fd_ >= 0) {
		close(stdin_fd_);
		stdin_fd_ = -1;
	}
	if (stdout_fd_ >= 0) {
		close(stdout_fd_);
		stdout_fd_ = -1;
	}
}

}  // namespace aicoder::mcp
