#pragma once

#include "mini_http/http_request.h"

#include <string>
#include <cstddef>

namespace mini_http {

enum class ReadResult {
    Complete,
    WouldBlock,
    BadRequest,
    PeerClosed,
    IoError
};

enum class WriteResult {
    Complete,
    WouldBlock,
    IoError
};

struct ConnectionState {
    std::string request_bytes;

    std::string response_bytes;
    std::size_t sent_bytes{0};
};

ReadResult read_request(
    int client_fd,
    ConnectionState& connection,
    HttpRequest& request
);

WriteResult flush_response(
    int client_fd,
    ConnectionState& connection
);


class EventLoop;

enum class ConnectionAction {
    Keep,
    Close
};

class Connection {
public:
    explicit Connection(int client_fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const noexcept;
    ConnectionState& state() noexcept;

    ReadResult read_request(HttpRequest& request);
    WriteResult flush_response();

    bool has_response() const noexcept;
    void set_response(std::string bytes);

    ConnectionAction handle_event(EventLoop& event_loop);


private:
    int client_fd_{-1};
    ConnectionState state_;
};

} // namespace
