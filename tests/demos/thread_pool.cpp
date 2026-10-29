#include "mini_http/thread_pool.h"

#include <atomic>
#include <iostream>

int main() {
    std::atomic<int> completed{0};

    {
        mini_http::ThreadPool pool(2);

        for(int i = 0; i < 100; i++) {
            pool.submit([&completed] {
                ++completed;
            });
        }
    }

    const int cnt = completed.load();

    std::cout << "completed = " << cnt << '\n';

    return cnt == 100 ? 0 : 1;
}