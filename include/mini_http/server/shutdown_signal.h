#pragma once

#include <signal.h>

namespace mini_http {

class ShutdownSignal {
public:
    ShutdownSignal();
    ~ShutdownSignal();

    ShutdownSignal(const ShutdownSignal&) = delete;
    ShutdownSignal& operator=(const ShutdownSignal&) = delete;

    int fd() const noexcept;

    bool consume(bool& received);

private:
    sigset_t original_mask_{};
    int signal_fd_{-1};
};


} // namespace