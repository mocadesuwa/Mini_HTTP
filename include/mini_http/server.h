#pragma once

#include "mini_http/connection.h"
#include "mini_http/acceptor.h"
#include "mini_http/event_loop.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace mini_http {

int create_listen_socket(std::uint16_t port);

bool send_all(
    int client_fd,
    std::string_view bytes
);

int run_server(std::uint16_t port);

class Server {
public:
    explicit Server(std::uint16_t port);
    int run();

private:
    bool handle_event(int fd, std::uint32_t events);

    Acceptor acceptor_;
    EventLoop event_loop_;
    std::unordered_map<int, Connection> connections_;
};



} // namespace

