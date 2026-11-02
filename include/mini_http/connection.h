#pragma once

#include "mini_http/http_request.h"

#include <string>
#include <cstddef>
#include <cstdint>
#include <chrono>

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

    std::chrono::steady_clock::time_point io_idle_since{
        std::chrono::steady_clock::now()
    };
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
    Close,
    RequestReady
};

class Connection {
public:
    explicit Connection(int client_fd, std::uint64_t id);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const noexcept;
    std::uint64_t id() const noexcept;
    bool processing() const noexcept;

    ConnectionState& state() noexcept;

    ReadResult read_request(HttpRequest& request);
    WriteResult flush_response();

    bool has_response() const noexcept;
    void set_response(std::string bytes);

    ConnectionAction handle_event(
        EventLoop& event_loop, HttpRequest& request);

    bool start_response(
        EventLoop& event_loop, std::string bytes, bool keep_alive = false);

    bool idle_timed_out(
        std::chrono::steady_clock::time_point now,
        std::chrono::seconds idle_timeout) const noexcept;

    ConnectionAction begin_shutdown() noexcept;

private:
    int client_fd_{-1};
    std::uint64_t id_{0};
    ConnectionState state_;
    bool processing_{false};

    bool keep_alive_{false};
};

} // namespace
