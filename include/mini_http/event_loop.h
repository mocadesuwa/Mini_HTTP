#pragma once

#include <cstdint>
#include <sys/epoll.h>
#include <functional>
#include <vector>
#include <mutex>

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
    using Task = std::function<void()>;

    int loop(
        const EventHandler& handler,
        int timeout_ms = -1,
        const Task& on_tick = {});

    bool wakeup();

    bool post(Task task);

private:
    int epoll_fd_{-1};
    int wakeup_fd_{-1};

    bool consume_wakeup();

    void run_pending_tasks();

    std::mutex pending_mutex_;
    std::vector<Task> pending_tasks_;
};

} // namespace