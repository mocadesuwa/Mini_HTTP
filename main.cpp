#include "mini_http/server.h"
#include "mini_http/logger.h"
#include "mini_http/config.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    mini_http::ServerConfig config;
    std::string error;

    const mini_http::ConfigParseResult result =
        mini_http::parse_arguments(argc, argv, config, error);

    if(result == mini_http::ConfigParseResult::Help) {
        const char* usage =
            "Usage: mini_http_server [--port PORT] [--root PATH] "
            "[--threads N] [--idle-timeout SECONDS] [--help]\n"
            "  --port PORT   Listen port (default: 8081)\n"
            "  --root PATH   Static file directory (default: www)\n"
            "  --threads N   Worker thread count (default: 2)\n"
            "  --idle-timeout SECONDS  IO idle timeout (default: 30 seconds)\n"
            "  --help        Show this help\n";

        std::cout << usage;
        return 0;
    } else if(result == mini_http::ConfigParseResult::Error) {
        mini_http::log(
            mini_http::LogLevel::Error,
            error);

        return 1;
    }

    return mini_http::run_server(config);
}