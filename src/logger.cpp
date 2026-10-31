#include "mini_http/logger.h"

#include <mutex>
#include <iostream>

namespace mini_http {
namespace {
    std::mutex log_mutex;
} // inner namespace

void log(LogLevel level, std::string_view message) {
    std::string_view prefix;

    switch(level) {
        case LogLevel::Info:
            prefix = "[INFO]";
            break;
        case LogLevel::Warning:
            prefix = "[WARN]";
            break;
        case LogLevel::Error:
            prefix = "[ERROR]";
            break;
    }

    std::lock_guard<std::mutex> lock(log_mutex);

    std::cerr << prefix << ' ' << message << '\n';
}

} // namespace