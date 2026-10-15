#pragma once

#include <format>
#include <cstdio>
#include <string_view>
#include <chrono>

namespace logger {

// Seconds since the first call (process start, in practice). Stamped onto
// track-switch / buffering trace lines so their phases can be timed from a log.
inline double uptime()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
    std::printf("[INFO]  %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
    std::fflush(stdout);
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args)
{
    std::printf("[WARN]  %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
    std::fflush(stdout);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
    std::fprintf(stderr, "[ERROR] %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
    std::fflush(stderr);
}

} // namespace logger
