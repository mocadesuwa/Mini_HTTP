#pragma once

#include <string_view>

namespace mini_http {

enum class LogLevel {
    Info,
    Warning,
    Error
};

void log(LogLevel level, std::string_view message);

} // namespace