#include "mini_http/reactor/event_loop.h"
#include "mini_http/runtime/thread_pool.h"

#include <iostream>
#include <thread>

int main() {
    mini_http::EventLoop event_loop;
    if(event_loop.fd() == -1) {
        return 1;
    }

    mini_http::ThreadPool pool(1);

    std::cout << "io: "
              << std::this_thread::get_id() << '\n';

    const bool submitted = pool.submit([&event_loop] {
        std::cout << "worker: "
                  << std::this_thread::get_id() << '\n';

        const bool posted = event_loop.post([]{
            std::cout << "callback: "
                      << std::this_thread::get_id() << '\n';
        });

        if(!posted) {
            std::cerr << "post failed\n";
        }
    });

    if(!submitted) {
        return 1;
    }

    return event_loop.loop([](int, std::uint32_t) {
        return true;
    });
}