#pragma once

#include <functional>
#include <cstddef>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <thread>

namespace mini_http {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(Task task);

private:
    void worker_loop();

    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

} // namespace