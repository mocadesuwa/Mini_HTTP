#include "mini_http/runtime/thread_pool.h"
#include "mini_http/server/server.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <iostream>

int main() {
    mini_http::EventLoop event_loop;
    const int notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if(notify_fd == -1) {
        std::perror("eventfd");
        return 1;
    }

    if(!event_loop.add_fd(notify_fd, EPOLLIN)) {
        std::perror("epoll_ctl ADD");
        close(notify_fd);
        return 1;
    }

    {
        mini_http::ThreadPool pool(1);

        pool.submit([notify_fd] {
            const std::uint64_t one = 1;
            const ssize_t n = write(notify_fd, &one, sizeof(one));
        });
    }

    bool success = false;
    epoll_event event{};
    const int ready_count = event_loop.wait(
        &event, 1, 3000);

    if(ready_count == -1) {
        std::perror("epoll_wait");
    } else if(ready_count == 0) {
        std::cerr << "wait timeout\n";
    } else if(ready_count == 1 &&
              event.data.fd == notify_fd &&
              (event.events & EPOLLIN) != 0){
        std::uint64_t counter = 0;
        const ssize_t n = read(
            notify_fd, &counter, sizeof(counter));
        if(n == 8 && counter == 1) {
            success = true;
            std::cout << "counter = " << counter << '\n';
        }

    } else {
        std::cerr << "unexpected error\n";
    }

    event_loop.remove_fd(notify_fd);
    close(notify_fd);

    return success ? 0 : 1;

}