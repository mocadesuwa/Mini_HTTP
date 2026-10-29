#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstdint>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <cstring>

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


int main() {
    int sockets[2]{};
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        std::perror("sockets: socketpair");
        return 1;
    }

    if(!set_nonblocking(sockets[0])) {
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }

    int sockets2[2]{};
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets2) == -1) {
        std::perror("sockets2: scoketpair");
        close(sockets[0]);
        close(sockets[1]);
        return 1;
    }

    if(!set_nonblocking(sockets2[0])) {
        close(sockets[0]);
        close(sockets[1]);
        close(sockets2[0]);
        close(sockets2[1]);
        return 1;
    }



    const int epoll_fd = epoll_create1(0);

    if(epoll_fd == -1) {
        std::perror("epoll_create1");
        close(sockets[0]);
        close(sockets[1]);
        close(sockets2[0]);
        close(sockets2[1]);
        return 1;
    }

    epoll_event interest{};
    interest.events = EPOLLIN | EPOLLET;
    interest.data.fd = sockets[0];

    if(epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        sockets[0],
        &interest) == -1 ){
            std::perror("epoll_ctl");
            close(epoll_fd);
            close(sockets[0]);
            close(sockets[1]);
            close(sockets2[0]);
            close(sockets2[1]);
            return 1;
    }

    epoll_event interest2{};
    interest2.events = EPOLLIN | EPOLLET;
    interest2.data.fd = sockets2[0];

    const int add_result = epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        sockets2[0],
        &interest2);

    if(add_result == -1) {
        std::perror("epoll_ctl");
        close(epoll_fd);
        close(sockets[0]);
        close(sockets[1]);
        close(sockets2[0]);
        close(sockets2[1]);
        return 1;
    }


    epoll_event ready_events[4]{};

    char buffer[64];

    bool io_ok = true;
    const ssize_t sent1 = send(sockets[1], "hello", 5, 0);
    const ssize_t sent2 = send(sockets2[1], "world", 5, 0);


    if(sent1 != 5 || sent2 != 5){
        std::cerr << "Failed to send both complete messages\n";
        io_ok = false;
    } else {
        const int ready_count = epoll_wait(
            epoll_fd, ready_events, 4, 0);

        if(ready_count == -1) {
            std::perror("epoll_wait after ready");
            close(epoll_fd);
            close(sockets[0]);
            close(sockets2[0]);
            close(sockets2[1]);

            if(sockets[1] != -1) {
                close(sockets[1]);
            }
            return 1;
        }

        for(int i = 0; i < ready_count; i++) {
            const int fd = ready_events[i].data.fd;

            const std::uint32_t flags = ready_events[i].events;

            if((flags & EPOLLHUP) != 0) {
                std::cout << "EPOLLHUP fd=" << fd << '\n';
            }

            if((flags & EPOLLERR) != 0) {
                std::cout << "EPOLLERR fd=" << fd << '\n';

                int socket_error = 0;
                socklen_t error_size = sizeof(socket_error);

                if(getsockopt(
                    fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) == -1) {
                    std::perror("getsockopt SO_ERROR");
                    io_ok = false;
                } else {
                    std::cout << "socket error: "
                              << std::strerror(socket_error) << '\n';
                }
            }

            if((flags & EPOLLIN) != 0) {

                while(true) {
                    const ssize_t n = recv(
                        fd, buffer, 2, 0);

                    if(n > 0) {
                        std::cout << "fd: " << fd << '\n'
                                << "length: " << n << '\n';
                        std::cout << "read: ";
                        std::cout.write(buffer, n);
                        std::cout << '\n';
                    }

                    if(n == 0) {
                        std::cout << "receive EOF" << '\n';
                        break;
                    }

                    if(n == -1) {
                        const int error_code = errno;

                        if(error_code == EINTR) {
                            continue;
                        }

                        if(error_code == EAGAIN || error_code == EWOULDBLOCK) {
                            std::cout << "WouldBlock\n";
                            break;
                        }

                        std::perror("recv");
                        io_ok = false;
                        break;
                    }
                }
            }
        }

        const int second_ready_count =
            epoll_wait(epoll_fd, ready_events, 4, 0);

        if(second_ready_count == -1) {
            io_ok = false;
        } else {
            std::cout << "second ready_count=" << second_ready_count << '\n';
        }
    }

    close(epoll_fd);
    close(sockets[0]);
    close(sockets2[0]);
    close(sockets2[1]);

    if(sockets[1] != -1) {
        close(sockets[1]);
    }

    return io_ok ? 0 : 1;
}