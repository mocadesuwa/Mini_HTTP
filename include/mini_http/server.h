#pragma once

#include "mini_http/connection.h"
#include "mini_http/acceptor.h"
#include "mini_http/event_loop.h"
#include "mini_http/thread_pool.h"
#include "mini_http/config.h"
#include "mini_http/shutdown_signal.h"


#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <chrono>

namespace mini_http {

int create_listen_socket(std::uint16_t port);

bool send_all(
    int client_fd,
    std::string_view bytes
);

int run_server(const ServerConfig& config);
int run_server(std::uint16_t port);

class Server {
public:

    explicit Server(const ServerConfig& config);
    explicit Server(std::uint16_t port);
    int run();

private:
    bool handle_event(int fd, std::uint32_t events);

    ShutdownSignal shutdown_signal_;
    Acceptor acceptor_;
    EventLoop event_loop_;
    std::unordered_map<int, Connection> connections_;

    // 编号分配
    std::uint64_t next_connection_id_{1};

    bool dispatch_request(
        int client_fd, std::uint64_t id, HttpRequest request);

    void close_connection(int client_fd);

    void close_idle_connections(std::chrono::seconds idle_timeout);

    const std::filesystem::path document_root_;

    const std::chrono::seconds idle_timeout_;

    void begin_shutdown();

    void on_tick();

    bool stopping_{false};
    std::chrono::steady_clock::time_point shutdown_deadline_{};

    ThreadPool thread_pool_;
};



} // namespace

