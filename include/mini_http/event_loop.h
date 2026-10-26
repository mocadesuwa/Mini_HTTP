#pragma once

#include <cstdint>
#include <sys/epoll.h>
#include <functional>

namespace mini_http {

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    int fd() const noexcept;

    bool add_fd(int fd, std::uint32_t events);
    bool modify_fd(int fd, std::uint32_t events);
    bool remove_fd(int fd);

    int wait(epoll_event* ready_events, int max_events, int timeout_ms);

    using EventHandler = std::function<bool(int, std::uint32_t)>;

    int loop(const EventHandler& handler);

private:
    int epoll_fd_{-1};
};

} // namespace