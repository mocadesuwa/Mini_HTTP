#include "mini_http/server.h"
#include "mini_http/http_parser.h"
#include "mini_http/http_handler.h"
#include "mini_http/http_response.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <exception>
#include <unordered_map>

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


ReadResult read_request(
    int client_fd, ConnectionState& connection, HttpRequest &request) {
    const HttpParser parser;

    std::string& request_bytes = connection.request_bytes;

    std::size_t consumed_bytes = 0;

    char buffer[4096];

    while (true) {
        ssize_t n = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            MSG_DONTWAIT);

        if(n > 0) {
            request_bytes.append(buffer, static_cast<std::size_t>(n));

            ParseResult result = parser.parse_request(
                    request_bytes, request, consumed_bytes);

            if(result == ParseResult::Complete) {
                return ReadResult::Complete;
            } else if(result == ParseResult::NeedMore) {
                continue;
            } else{
                return ReadResult::BadRequest;
            }
        }

        if(n == 0) {
            if(request_bytes.empty()) {
                return ReadResult::PeerClosed;
            }

            return ReadResult::BadRequest;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadResult::WouldBlock;
        }

        const int error_code = errno;
        std::cerr << "recv failed: "
                  << std::strerror(error_code) << '\n';

        return ReadResult::IoError;
    }


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

WriteResult flush_response(
    int client_fd, ConnectionState &connection) {

    while(connection.sent_bytes < connection.response_bytes.size()) {
        const ssize_t n = send(
            client_fd,
            connection.response_bytes.data() + connection.sent_bytes,
            connection.response_bytes.size() - connection.sent_bytes,
            MSG_NOSIGNAL | MSG_DONTWAIT);

        if(n > 0) {
            connection.sent_bytes +=
                static_cast<std::size_t>(n);
            continue;
        }

        if(n < 0) {
            if(errno == EINTR) {
                continue;
            }

            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return WriteResult::WouldBlock;
            }
        }
        return WriteResult::IoError;
    }

    return WriteResult::Complete;
}

namespace  {

bool watch_writable(int epoll_fd, int client_fd) {
    epoll_event interest{};
    interest.events = EPOLLOUT;
    interest.data.fd = client_fd;

    if(epoll_ctl(
        epoll_fd, EPOLL_CTL_MOD, client_fd, &interest) == -1) {
        std::perror("error_ctl MOD");
        return false;
    }

    return true;
}

} // inner namespace

int run_server(std::uint16_t port) {
    int server_fd = -1;

    try {
        server_fd = create_listen_socket(port);
    } catch (const std::exception& e) {
        std::cerr << "Server initialization failed: "
                  << e.what() << "\n";

        return -1;
    }

    if(server_fd < 0) {
        return -1;
    }

    if(!set_nonblocking(server_fd)) {
        close(server_fd);
        return -1;
    }

    // 创建epoll

    const int epoll_fd = epoll_create1(0);

    if(epoll_fd == -1) {
        std::perror("epoll_create1");
        close(server_fd);
        return -1;
    }

    epoll_event interest{};
    interest.events = EPOLLIN;
    interest.data.fd = server_fd;

    if(epoll_ctl(
        epoll_fd, EPOLL_CTL_ADD, server_fd, &interest) == -1) {
            std::perror("epoll_ctl");
            close(epoll_fd);
            close(server_fd);
            return -1;
    }

    epoll_event ready_events[4]{};

    std::unordered_map<int, ConnectionState> connections;


    while(true) {
        const int ready_count = epoll_wait(
            epoll_fd, ready_events, 4, -1);

        if(ready_count == -1) {
            if(errno == EINTR) {
                continue;
            }

            std::perror("epoll_wait");
            close(epoll_fd);
            close(server_fd);
            return -1;
        }

        for(int i = 0; i < ready_count; i++) {
            const int fd = ready_events[i].data.fd;

            const std::uint32_t events = ready_events[i].events;
            if((events & EPOLLHUP) != 0) {
                std::cout << "EPOLLHUP fd=" << fd << '\n';
            }

            if((events & EPOLLERR) != 0) {
                std::cout << "EPOLLERR fd=" << fd << '\n';
            }

            if(fd == server_fd) {
                const int client_fd = accept(
                    server_fd, nullptr, nullptr);

                if(client_fd == -1) {
                    if(errno == EAGAIN ||
                    errno == EWOULDBLOCK ||
                    errno == EINTR) {
                        continue;
                    }

                std::perror("accept");
                close(epoll_fd);
                close(server_fd);
                return -1;
                }
                connections.emplace(client_fd, ConnectionState{});

                epoll_event client_interest{};
                client_interest.events = EPOLLIN;
                client_interest.data.fd = client_fd;

                if(epoll_ctl(
                    epoll_fd, EPOLL_CTL_ADD, client_fd, &client_interest) == -1) {
                    std::perror("epoll_ctl client");
                    connections.erase(client_fd);
                    close(client_fd);
                }

                continue;
            }

            auto it = connections.find(fd);

            if(it == connections.end()) {
                continue;
            }

            const int client_fd = fd;
            ConnectionState& connection = it->second;

            if(!connection.response_bytes.empty()) {
                const WriteResult write_result =
                    flush_response(client_fd, connection);

                if(write_result == WriteResult::WouldBlock) {
                    continue;
                }

                if(write_result == WriteResult::IoError) {
                    std::cerr << "Failed to send response\n";
                }
            } else {
                HttpRequest request;

                const ReadResult result = read_request(
                    client_fd, connection, request);

                switch(result) {
                    case ReadResult::Complete: {
                        const HttpResponse response = handle_request(request);

                        connection.response_bytes = response.serialize();
                        connection.sent_bytes = 0;

                        if(watch_writable(epoll_fd, client_fd)) {
                            continue;
                        }
                        break;
                    }

                    case ReadResult::WouldBlock:
                        continue;

                    case ReadResult::PeerClosed:
                        break;

                    case ReadResult::BadRequest: {
                        HttpResponse response;
                        response.status_code = 400;
                        response.body = "Bad request\n";

                        connection.response_bytes = response.serialize();
                        connection.sent_bytes = 0;

                        if(watch_writable(epoll_fd, client_fd)) {
                            continue;
                        }
                        break;
                    }

                    case ReadResult::IoError:
                        std::cerr << "Failed to read request\n";
                        break;
                }
            }

            if(epoll_ctl(
                epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) == -1) {
                    std::perror("epoll_ctl DEL");
            }

            connections.erase(client_fd);
            close(client_fd);
        }
    }
}


} // namespace