#include "mini_http/server.h"
#include "mini_http/http_handler.h"
#include "mini_http/http_response.h"
#include "mini_http/event_loop.h"
#include "mini_http/acceptor.h"
#include "mini_http/logger.h"

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
#include <string>
#include <system_error>

namespace mini_http {

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);

    if(flags == -1) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("fcntl F_GETFL failed: ") + std::strerror(error_code));
        return false;
    }

    const int new_flags = flags | O_NONBLOCK;

    const int result = fcntl(fd, F_SETFL, new_flags);

    if(result == -1) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("fcntl F_SETFL failed: ") + std::strerror(error_code));

        return false;
    }

    return result == 0;
}

int create_listen_socket(std::uint16_t port) {
    int listen_fd = socket(AF_INET,
                            SOCK_STREAM,
                            0);

    if(listen_fd < 0) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("socket failed: ") + std::strerror(error_code));

        return -1;
    }

    int opt = 1;
    if(setsockopt(listen_fd,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    &opt,
                    sizeof(opt)) < 0) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("setsockopt failed: ") + std::strerror(error_code));

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
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("bind failed: ") + std::strerror(error_code));

        close(listen_fd);
        return -1;
    }

    if(listen(listen_fd, 10) < 0) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("listen failed: ") + std::strerror(error_code));

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

Server::Server(const ServerConfig& config) :
    acceptor_ (create_listen_socket(config.port)),
    document_root_(std::filesystem::absolute(config.document_root)),
    idle_timeout_(config.idle_timeout),
    thread_pool_(config.thread_count) {

}

//  委托构造
Server::Server(std::uint16_t port)
    : Server(ServerConfig{port}) {
}

int Server::run() {
    if(acceptor_.fd() == -1) {
        log(LogLevel::Error,
            "listen socket initialization failed");
        return -1;
    }

    if(!set_nonblocking(acceptor_.fd())) {
        return -1;
    }

    if(event_loop_.fd() == -1) {
        log(LogLevel::Error,
            "event loop initialization failed");
        return -1;
    }

    if(!event_loop_.add_fd(acceptor_.fd(), EPOLLIN)) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("epoll_ctl ADD listen socket failed: ") +
            std::strerror(error_code));
        return -1;
    }

    log(LogLevel::Info, "server started");

    return event_loop_.loop(
        [this](int fd, std::uint32_t events) {
            return handle_event(fd, events);
        },
        1000,
        [this] {
            close_idle_connections(idle_timeout_);
        }
    );
}

bool Server::handle_event(int fd, std::uint32_t events) {
    if((events & EPOLLHUP) != 0) {
        log(LogLevel::Info,
            std::string("EPOLLHUP fd=") + std::to_string(fd));
    }

    if((events & EPOLLERR) != 0) {
        log(LogLevel::Error,
            std::string("EPOLLERR fd=") + std::to_string(fd));
    }

    if(fd == acceptor_.fd()) {
        const int client_fd = acceptor_.accept_one();

        if(client_fd == -1) {
            if(errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR) {
                return true;
            }
            const int error_code = errno;

            log(LogLevel::Error,
                std::string("accept failed: ") + std::strerror(error_code));
            return false;
        }
        connections_.try_emplace(
            client_fd, client_fd, next_connection_id_++);

        if(!event_loop_.add_fd(client_fd, EPOLLIN)) {
            const int error_code = errno;
            log(LogLevel::Error,
                std::string("epoll_ctl ADD client failed: ") + std::strerror(error_code));

            connections_.erase(client_fd);
        } else {
            log(LogLevel::Info,
                std::string("accepted connection fd=") + std::to_string(client_fd));
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

int run_server(const ServerConfig &config) {
    try {
        std::error_code ec;

        const std::filesystem::path root =
            std::filesystem::canonical(config.document_root, ec);

        if(ec) {
            log(LogLevel::Error,
                std::string("invalid --root: ") + ec.message());
            return -1;
        }

        const bool root_is_directory =
            std::filesystem::is_directory(root, ec);

        if(ec) {
            log(LogLevel::Error,
                std::string("failed to inspect --root: ") + ec.message());
            return -1;
        }

        if(!root_is_directory) {
            log(LogLevel::Error,
                "--root must refer to a directory");
            return -1;
        }

        ServerConfig validated_config = config;
        validated_config.document_root = root;

        Server server(validated_config);
        return server.run();
    } catch(const std::exception& error) {
        log(LogLevel::Error,
            std::string("server failed: ") + error.what());

        return -1;
    }
}

//  委托
int run_server(std::uint16_t port) {
    ServerConfig config;
    config.port = port;
    return run_server(config);
}

bool Server::dispatch_request(
    int client_fd, std::uint64_t id, HttpRequest request) {

    if(!event_loop_.modify_fd(client_fd, 0)) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("epoll_ctl MOD pause failed: ") + std::strerror(error_code));
        return false;
    }

    const bool keep_alive = should_keep_alive(request);

    return thread_pool_.submit(
        [this, client_fd, id, request = std::move(request), keep_alive] {
            std::string bytes;
            bool succeeded = false;

            try {
                HttpResponse response = handle_request(request, document_root_);
                response.keep_alive = keep_alive;
                bytes = response.serialize();

                succeeded = true;
            } catch(...) {
                log(LogLevel::Error, "HTTP handler failed");
            }

            const bool posted = event_loop_.post(
                [this, client_fd, id, succeeded, keep_alive,
                bytes = std::move(bytes)]() mutable {
                    auto it = connections_.find(client_fd);

                    if(it == connections_.end() || it->second.id() != id) {
                        return;
                    }

                    if(!succeeded ||
                        !it->second.start_response(
                            event_loop_, std::move(bytes), keep_alive)) {
                        close_connection(client_fd);
                    }
                }
            );

            if(!posted) {
                log(LogLevel::Error, "post response failed");
            }

        });
}

void Server::close_connection(int client_fd) {
    if(!event_loop_.remove_fd(client_fd)) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("epoll_ctl DEL failed: ") + std::strerror(error_code));
    }

    const std::size_t removed = connections_.erase(client_fd);

    if(removed == 1) {
        log(LogLevel::Info,
            std::string("closed connection fd=") +
            std::to_string(client_fd));
    }
}

void Server::close_idle_connections(std::chrono::seconds idle_timed_out) {
    const auto now = std::chrono::steady_clock::now();

    auto it = connections_.begin();

    while(it != connections_.end()) {
        const int client_fd = it->first;
        const bool timed_out =
            it->second.idle_timed_out(now, idle_timed_out);

        ++it;

        if(timed_out) {
            log(LogLevel::Info,
                std::string("idle timeout fd=") + std::to_string(client_fd));
            close_connection(client_fd);
        }
    }
}

} // namespace