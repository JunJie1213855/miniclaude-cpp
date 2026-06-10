#pragma once
// 统一日志门面 —— 封装 spdlog,便于后续切 sink / 改等级。
// 双 sink:文件落盘到 ~/.aicoder/log/aicoder-{YYYY-MM-DD_HHMMSS}.log,
// 同时 stdout 彩色输出(便于交互会话直接看)。
// 等级 = info(可通过环境变量 SPDLOG_LEVEL 覆盖,例如 SPDLOG_LEVEL=debug)。
// 用法:
//   #include "util/Log.h"
//   AICODER_LOG_INFO("user clicked button, id={}", id);
//   AICODER_LOG_DEBUG("detailed trace");   // 等级低于默认,不输出
//   AICODER_LOG_WARN("permission denied for tool={}", tool);
//
// 环境变量:
//   SPDLOG_LEVEL = trace/debug/info/warn/error/critical/off(默认 info)
//   AICODER_LOG_DIR = 自定义日志目录(默认 ~/.aicoder/log)

// 必须在 include spdlog 之前打开 SPDLOG_USE_STD_FORMAT,
// 让 spdlog 1.12 用 C++20 std::format 而不是它内嵌的 fmt。
#ifndef SPDLOG_USE_STD_FORMAT
#define SPDLOG_USE_STD_FORMAT
#endif
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace aicoder {

namespace detail {

// 解析 ISO-like 时间戳串(YYYY-MM-DD_HHMMSS),用作日志文件名。
inline std::string timestampForFilename()
{
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H%M%S");
    return oss.str();
}

// 解析日志目录。env AICODER_LOG_DIR 优先(可指定绝对路径);
// 否则默认放在程序当前工作目录下的 ./log/(便于 "cd 到哪里就在哪里")。
// 目录不存在则创建(create_directories 是 no-op 如果已存在)。
inline std::filesystem::path resolveLogDir()
{
    const char* env = std::getenv("AICODER_LOG_DIR");
    std::filesystem::path dir =
        env && *env ? std::filesystem::path(env)
                    : std::filesystem::current_path() / "log";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}  // namespace detail

// 全局 logger。首次调用时按需初始化,带 stdout + 文件 双 sink。
inline std::shared_ptr<spdlog::logger> log()
{
    static auto instance = []() {
        std::vector<spdlog::sink_ptr> sinks;

        // 1) stdout 彩色 sink(便于实时看)
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        sinks.push_back(console);

        // 2) 文件 sink(落盘持久化,默认 ~/.aicoder/log/aicoder-{ts}.log)
        try
        {
            auto dir = detail::resolveLogDir();
            auto path = dir / ("aicoder-" + detail::timestampForFilename() + ".log");
            // truncate 模式:每次启动一个新文件(便于定位问题时间窗)。
            // 如果想保留历史可换 spdlog::filemode::append。
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                path.string(), /*truncate=*/true);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file);
            // 把路径写到 stderr 一行,告诉用户日志在哪。
            std::fprintf(stderr, "[log] writing diagnostics to %s\n", path.string().c_str());
        }
        catch (const std::exception& e)
        {
            // 文件 sink 创建失败(权限/磁盘满)——只保留 stdout,不阻断程序启动。
            std::fprintf(stderr, "[log] file sink init failed: %s (fallback to stdout only)\n",
                         e.what());
        }

        auto l = std::make_shared<spdlog::logger>("aicoder", sinks.begin(), sinks.end());
        // 默认 info。env SPDLOG_LEVEL 可覆盖。
        const char* lvl = std::getenv("SPDLOG_LEVEL");
        l->set_level(lvl ? spdlog::level::from_str(lvl) : spdlog::level::info);
        // warn+ 立即冲刷(避免崩溃丢日志);info 走 spdlog 默认 buffer。
        l->flush_on(spdlog::level::warn);
        // 注册为默认 logger,后续 spdlog::info(...) 也能工作
        spdlog::set_default_logger(l);
        return l;
    }();
    return instance;
}

}  // namespace aicoder

// 便捷宏
#define AICODER_LOG_TRACE(...)    ::aicoder::log()->trace(__VA_ARGS__)
#define AICODER_LOG_DEBUG(...)    ::aicoder::log()->debug(__VA_ARGS__)
#define AICODER_LOG_INFO(...)     ::aicoder::log()->info(__VA_ARGS__)
#define AICODER_LOG_WARN(...)     ::aicoder::log()->warn(__VA_ARGS__)
#define AICODER_LOG_ERROR(...)    ::aicoder::log()->error(__VA_ARGS__)
#define AICODER_LOG_CRITICAL(...) ::aicoder::log()->critical(__VA_ARGS__)