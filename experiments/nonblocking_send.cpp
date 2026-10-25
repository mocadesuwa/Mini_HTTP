#include "mini_http/server.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <cerrno>

int main() {
    // sockets: int[2]，两个已经连接的端点
    int sockets[2]{};

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        std::perror("socketpair");
        return 1;
    }

    // send_buffer_size: const int，请求较小的发送缓冲区
    const int send_buffer_size = 4096;

    if (setsockopt(
            sockets[0],
            SOL_SOCKET,
            SO_SNDBUF,
            &send_buffer_size,
            sizeof(send_buffer_size)) == -1) {
        std::perror("setsockopt SO_SNDBUF");
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }

    mini_http::ConnectionState connection;
    connection.response_bytes = std::string(1024 * 1024, 'x');

    // 接收端 sockets[1] 保持打开，但暂时不读取
    const mini_http::WriteResult result =
        mini_http::flush_response(sockets[0], connection);

    const bool would_block =
        result == mini_http::WriteResult::WouldBlock;

    std::cout << std::boolalpha;
    std::cout << "WouldBlock: " << would_block << '\n';
    std::cout << "sent_bytes: " << connection.sent_bytes << '\n';
    std::cout << "total_bytes: "
              << connection.response_bytes.size() << '\n';

    const int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) {
        std::perror("epoll_creat1");
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }

    epoll_event interest{};
    interest.events = EPOLLOUT;
    interest.data.fd = sockets[0];

    if(epoll_ctl(
        epoll_fd, EPOLL_CTL_ADD, sockets[0], &interest) == -1) {
        std::perror("epoll_ctl ADD");
        close(epoll_fd);
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }


    char buffer[4096];

    std::string received_data;
    mini_http::WriteResult write_result = result;

    while(true) {
        while(true) {
            const ssize_t n = recv(
                sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT);

            if(n > 0) {
                received_data.append(buffer, static_cast<std::size_t>(n));
                continue;
            }

            if(n < 0 && errno == EINTR) {
                continue;
            }

            if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
        }

        if(write_result == mini_http::WriteResult::Complete) {
            break;
        }

        epoll_event ready{};
        const int ready_count =
            epoll_wait(epoll_fd, &ready, 1, 1000);

        const bool writable =
            ready_count == 1 &&
            ready.data.fd == sockets[0] &&
            (ready.events & EPOLLOUT) != 0;

        if(!writable) {
            std::cerr << "No writable event\n";
            close(epoll_fd);
            close(sockets[0]);
            close(sockets[1]);
            return 1;
        }

        write_result =
            mini_http::flush_response(sockets[0], connection);

        if(write_result == mini_http::WriteResult::IoError) {
            close(epoll_fd);
            close(sockets[0]);
            close(sockets[1]);
            return 1;
        }
    }

    const bool passed =
        would_block &&
        write_result == mini_http::WriteResult::Complete &&
        connection.sent_bytes == connection.response_bytes.size() &&
        received_data == connection.response_bytes;

    std::cout << "passed: " << passed << '\n';

    close(epoll_fd);
    close(sockets[0]);
    close(sockets[1]);

    return passed ? 0 : 1;

}