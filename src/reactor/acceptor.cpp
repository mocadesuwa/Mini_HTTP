#include "mini_http/reactor/acceptor.h"

#include <sys/socket.h>
#include <unistd.h>

namespace mini_http {

Acceptor::Acceptor(int listen_fd) noexcept
    : listen_fd_(listen_fd) {
}

Acceptor::~Acceptor() {
    stop();
}

int Acceptor::fd() {
    return listen_fd_;
}

int Acceptor::accept_one() {
    return accept(listen_fd_, nullptr, nullptr);
}

void Acceptor::stop() noexcept {
    if(listen_fd_ < 0) {
        return;
    }

    close(listen_fd_);
    listen_fd_ = -1;
}

} // namespace