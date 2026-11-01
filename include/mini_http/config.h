#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace mini_http {

struct ServerConfig {
    std::uint16_t port{8081};
    std::filesystem::path document_root{"www"};
    std::size_t thread_count{2};
};

enum class ConfigParseResult {
    Success,
    Help,
    Error
};

ConfigParseResult parse_arguments(
    int argc,
    const char* const argv[],
    ServerConfig& config,
    std::string& error);


} // namespace