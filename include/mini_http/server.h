#pragma once

#include "mini_http/http_request.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <cstddef>

namespace mini_http {

enum class ReadResult {
    Complete,
    WouldBlock,
    BadRequest,
    PeerClosed,
    IoError
};

struct ConnectionState {
    std::string request_bytes;

    std::string response_bytes;
    std::size_t sent_bytes{0};
};

int create_listen_socket(std::uint16_t port);


ReadResult read_request(
    int client_fd,
    ConnectionState& connection,
    HttpRequest& request
);


bool send_all(
    int client_fd,
    std::string_view bytes
);

enum class WriteResult {
    Complete,
    WouldBlock,
    IoError
};

WriteResult flush_response(
    int client_fd,
    ConnectionState& connection
);

int run_server(std::uint16_t port);

}