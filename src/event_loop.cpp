#include "mini_http/event_loop.h"
#include "mini_http/logger.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <utility>
#include <string>
#include <cstring>

namespace mini_http {

EventLoop::EventLoop() :
    epoll_fd_(epoll_create1(0)) {

    if(epoll_fd_ == -1) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("epoll_create1 failed: ") + std::strerror(error_code));
        return;
    }

    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if(wakeup_fd_ == -1) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("eventfd failed: ") + std::strerror(error_code));

        close(epoll_fd_);
        epoll_fd_ = -1;
        return;
    }

    if(!add_fd(wakeup_fd_, EPOLLIN)) {
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("epoll_ctl wakeup failed: ") + std::strerror(error_code));

        close(wakeup_fd_);
        wakeup_fd_ = -1;

        close(epoll_fd_);
        epoll_fd_ = -1;

        return;
    }
}

EventLoop::~EventLoop() {
    if(wakeup_fd_ >= 0) {
        close(wakeup_fd_);
    }

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

int EventLoop::loop(
    const EventHandler& handler,
    int timeout_ms,
    const Task& on_tick,
    const StopCondition& should_stop) {
    epoll_event ready_events[4]{};

    while(true) {
        if(should_stop && should_stop()) {
            return 0;
        }
        int ready_count = wait(ready_events, 4, timeout_ms);

        if(ready_count == -1) {
            const int error_code = errno;

            if(errno == EINTR) {
                ready_count = 0;
            } else {
                log(LogLevel::Error,
                    std::string("epoll_wait failed: ") + std::strerror(error_code));
                return -1;
            }
        }

        for(int i = 0; i < ready_count; i++) {
            const int fd = ready_events[i].data.fd;

            const std::uint32_t events = ready_events[i].events;

            if(fd == wakeup_fd_) {
                if(!consume_wakeup()) {
                    return -1;
                }

                run_pending_tasks();

                continue;
            }

            if(!handler(fd, events)) {
                return -1;
            }
        }

        if(on_tick) {
            on_tick();
        }
    }
}

bool EventLoop::wakeup() {
    const std::uint64_t one{1};

    while(true) {
        const ssize_t n = write(wakeup_fd_, &one, sizeof(one));

        if(n == static_cast<ssize_t>(sizeof(one))) {
            return true;
        }

        if(n == -1 && errno == EINTR) {
            continue;
        }

        if(n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }

        const int error_code = errno;

        log(LogLevel::Error,
            std::string("eventfd write failed: ") + std::strerror(error_code));

        return false;
    }
}

bool EventLoop::consume_wakeup() {
    std::uint64_t counter = 0;

    while(true) {
        const ssize_t n = read(
            wakeup_fd_, &counter, sizeof(counter));

        if(n == static_cast<ssize_t>(sizeof(counter))) {
            return true;
        }

        if(n == -1 && errno == EINTR) {
            continue;
        }

        if(n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        const int error_code = errno;

        log(LogLevel::Error,
            std::string("eventfd read failed: ") + std::strerror(error_code));

        return false;
    }
}

bool EventLoop::post(Task task) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_tasks_.push_back(std::move(task));
    }

    return wakeup();
}

void EventLoop::run_pending_tasks() {
    std::vector<Task> tasks;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        tasks.swap(pending_tasks_);
    }

    for(Task& task : tasks) {
        task();
    }
}


} // namespace