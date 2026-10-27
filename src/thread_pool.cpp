#include "mini_http/thread_pool.h"
#include <utility>
#include <stdexcept>

namespace mini_http {

void ThreadPool::worker_loop() {
    while(true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            condition_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });

            if(stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();
    }
}

ThreadPool::ThreadPool(std::size_t thread_count) {
    if(thread_count == 0) {
        throw std::invalid_argument(
            "thread count must be greater than 0");
    }

    try {
        for(std::size_t i = 0; i < thread_count; i++) {
            workers_.emplace_back([this]() {
                worker_loop();
            });
        }
    } catch(...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }

        condition_.notify_all();

        for(std::thread& worker : workers_) {
            worker.join();
        }

        throw;
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

    condition_.notify_all();
    for(std::thread& worker : workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

bool ThreadPool::submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_) {
            return false;
        }
        tasks_.emplace(std::move(task));

    }
    condition_.notify_one();
    return true;
}


} // namespace