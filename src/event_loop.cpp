#include "mini_http/event_loop.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>

namespace mini_http {

EventLoop::EventLoop() :
    epoll_fd_(epoll_create1(0)) {
}

EventLoop::~EventLoop() {
    if(epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

int EventLoop::fd() const noexcept {
    return epoll_fd_;
}

bool EventLoop::add_fd(int fd, std::uint32_t events) {
    epoll_event interest{};
    interest.events = events;
    interest.data.fd = fd;

    return epoll_ctl(
        epoll_fd_, EPOLL_CTL_ADD, fd, &interest) == 0;
}

bool EventLoop::modify_fd(int fd, std::uint32_t events) {
    epoll_event interest{};
    interest.events = events;
    interest.data.fd = fd;

    return epoll_ctl(
        epoll_fd_, EPOLL_CTL_MOD, fd, &interest) == 0;
}

bool EventLoop::remove_fd(int fd) {
    return epoll_ctl(
        epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EventLoop::wait(
    epoll_event* ready_events, int max_events, int timeout_ms) {
    return epoll_wait(
        epoll_fd_, ready_events, max_events, timeout_ms);
}

int EventLoop::loop(const EventHandler& handler) {
    epoll_event ready_events[4]{};

    while(true) {
        const int ready_count = wait(
            ready_events, 4, -1);

        if(ready_count == -1) {
            if(errno == EINTR) {
                continue;
            }

            std::perror("epoll_wait");
            return -1;
        }

        for(int i = 0; i < ready_count; i++) {
            const int fd = ready_events[i].data.fd;

            const std::uint32_t events = ready_events[i].events;

            if(!handler(fd, events)) {
                return -1;
            }
        }
    }
}


} // namespace