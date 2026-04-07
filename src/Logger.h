#pragma once

#include <format>
#include <cstdio>
#include <string_view>

namespace logger {

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
