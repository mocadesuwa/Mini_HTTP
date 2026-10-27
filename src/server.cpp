#include "mini_http/server.h"
#include "mini_http/http_handler.h"
#include "mini_http/http_response.h"
#include "mini_http/event_loop.h"
#include "mini_http/acceptor.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <exception>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <sys/epoll.h>
#include <cstdio>


namespace mini_http {

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);

    if(flags == -1) {
        std::perror("fcntl F_GETFL");
        return false;
    }

    const int new_flags = flags | O_NONBLOCK;

    const int result = fcntl(fd, F_SETFL, new_flags);

    return result == 0;
}

int create_listen_socket(std::uint16_t port) {
    int listen_fd = socket(AF_INET,
                            SOCK_STREAM,
                            0);

    if(listen_fd < 0) {
        std::cerr << "socket failed: "
                  << std::strerror(errno) << '\n';
        return -1;
    }

    int opt = 1;
    if(setsockopt(listen_fd,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    &opt,
                    sizeof(opt)) < 0) {
        std::cerr << "setsocket failed: "
                  << std::strerror(errno) << '\n';
        close(listen_fd);
        return -1;
    }

    sockaddr_in listen_addr{};

    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = htons(port);

    if(bind(listen_fd,
            reinterpret_cast<sockaddr*>(&listen_addr),
            sizeof(listen_addr))  < 0) {
        std::cerr << "bind failed: "
                  << std::strerror(errno) << '\n';

        close(listen_fd);
        return -1;
    }

    if(listen(listen_fd, 10) < 0) {
        std::cerr << "listen failed: "
                  << std::strerror(errno) << '\n';

        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

bool send_all(int client_fd, std::string_view bytes) {
    std::size_t total_sent = 0;

    while(total_sent < bytes.size()) {
        ssize_t n = send(
            client_fd,
            bytes.data() + total_sent,
            bytes.size() - total_sent,
            MSG_NOSIGNAL
        );

        if(n > 0) {
            total_sent += n;
            continue;
        }

        if(n < 0 && errno == EINTR) {
            continue;
        }

        if(n < 0) {
            const int error_code = errno;
            std::cerr << "send failed: "
                      << std::strerror(error_code) << '\n';
        } else {
            std::cerr << "send returned 0 with data remaining\n";
        }
        return false;

    }
    return true;
}

Server::Server(std::uint16_t port)
    : acceptor_(create_listen_socket(port)) {
}

int Server::run() {
    if(acceptor_.fd() == -1) {
        std::perror("accerptor_fd");
        return -1;
    }

    if(!set_nonblocking(acceptor_.fd())) {
        return -1;
    }

    if(event_loop_.fd() == -1) {
        std::perror("epoll_create1");
        return -1;
    }

    if(!event_loop_.add_fd(acceptor_.fd(), EPOLLIN)) {
        std::perror("add_fd");
        return -1;
    }

    return event_loop_.loop(
        [this](int fd, std::uint32_t events) {
            return handle_event(fd, events);
        }
    );
}

bool Server::handle_event(int fd, std::uint32_t events) {
    if((events & EPOLLHUP) != 0) {
        std::cout << "EPOLLHUP fd=" << fd << '\n';
    }

    if((events & EPOLLERR) != 0) {
        std::cout << "EPOLLERR fd=" << fd << '\n';
    }

    if(fd == acceptor_.fd()) {
        const int client_fd = acceptor_.accept_one();

        if(client_fd == -1) {
            if(errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR) {
                return true;
            }

            std::perror("accept");
            return false;
        }
        connections_.try_emplace(
            client_fd, client_fd, next_connection_id_++);

        if(!event_loop_.add_fd(client_fd, EPOLLIN)) {
            std::perror("epoll_ctl client");
            connections_.erase(client_fd);
        }

        return true;
    }

    auto it = connections_.find(fd);

    if(it == connections_.end()) {
        return true;
    }

    Connection& connection = it->second;
    const int client_fd = connection.fd();

    // 处理完整请求已提交， 正在等待业务结果
    if(connection.processing()) {
        if((events & (EPOLLHUP | EPOLLERR)) != 0) {
            close_connection(client_fd);
        }
        return true;
    }

    HttpRequest request;
    ConnectionAction action = connection.handle_event(
        event_loop_, request);

    if(action == ConnectionAction::RequestReady) {
        const bool submitted = dispatch_request(
            client_fd, connection.id(), std::move(request));

        action = submitted ? ConnectionAction::Keep : ConnectionAction::Close;
    }

    if(action == ConnectionAction::Close) {
        close_connection(client_fd);
    }

    return true;
}

int run_server(std::uint16_t port) {
    Server server(port);
    return server.run();
}

bool Server::dispatch_request(
    int client_fd, std::uint64_t id, HttpRequest request) {

    if(!event_loop_.modify_fd(client_fd, 0)) {
        std::perror("epoll_ctl pause");
        return false;
    }

    return thread_pool_.submit(
        [this, client_fd, id, request = std::move(request)] {
            std::string bytes;
            bool succeeded = false;

            try {
                bytes = handle_request(request).serialize();
                succeeded = true;
            } catch(...) {
                std::cerr << "HTTP handler failed\n";
            }

            const bool posted = event_loop_.post(
                [this, client_fd, id, succeeded,
                bytes = std::move(bytes)]() mutable {
                    auto it = connections_.find(client_fd);

                    if(it == connections_.end() || it->second.id() != id) {
                        return;
                    }

                    if(!succeeded ||
                        !it->second.start_response(event_loop_, std::move(bytes))) {
                        close_connection(client_fd);
                    }
                }
            );

            if(!posted) {
                std::cerr << "post response failed\n";
            }

        });
}

void Server::close_connection(int client_fd) {
    if(!event_loop_.remove_fd(client_fd)) {
        std::perror("epoll_ctl DEL");
    }

    connections_.erase(client_fd);
}

} // namespace