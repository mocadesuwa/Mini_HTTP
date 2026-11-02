#include "mini_http/server/shutdown_signal.h"
#include "mini_http/runtime/logger.h"

#include <pthread.h>
#include <sys/signalfd.h>
#include <cerrno>
#include <system_error>
#include <unistd.h>
#include <string>
#include <cstring>


namespace mini_http {

ShutdownSignal::ShutdownSignal() {
    sigset_t mask{};

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    const int mask_result =
        pthread_sigmask(SIG_BLOCK, &mask, &original_mask_);

    if(mask_result != 0) {
        throw std::system_error(
            mask_result, std::generic_category(), "pthread_sigmask");
    }

    signal_fd_ = signalfd(
        -1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    if(signal_fd_ == -1) {
        const int error_code = errno;

        pthread_sigmask(SIG_SETMASK, &original_mask_, nullptr);

        throw std::system_error(
            error_code, std::generic_category(), "signalfd");
    }
}

ShutdownSignal::~ShutdownSignal() {
    int close_error = 0;

    if(signal_fd_ >= 0) {
        bool received = false;

        try {
            consume(received);
        } catch (...) {
            // 尽力消费失败后，仍继续关闭 fd 和恢复信号屏蔽字。
        }
        if(::close(signal_fd_) == -1) {
            close_error = errno;
        }
        signal_fd_ = -1;
    }

    const int restore_result =
        pthread_sigmask(SIG_SETMASK, &original_mask_, nullptr);

    try {
        if(close_error != 0) {
            log(LogLevel::Error,
            std::string("signalfd close failed: ") +
            std::strerror(close_error));
        }

        if(restore_result != 0) {
            log(LogLevel::Error,
                std::string("pthread_sigmask restore failed: ") +
                std::strerror(restore_result));
        }

    } catch (...) {
        // 清理已完成，忽略诊断异常，避免异常逃出析构函数。
    }
}

int ShutdownSignal::fd() const noexcept {
    return signal_fd_;
}

bool ShutdownSignal::consume(bool& received) {
    received = false;

    while(true) {
        signalfd_siginfo info{};

        const ssize_t n = ::read(
            signal_fd_, &info, sizeof(info));

        if(n == static_cast<ssize_t>(sizeof(info))) {
            received = true;
            continue;
        }

        if(n == -1) {
            if(errno == EINTR) {
                continue;
            }

            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            const int error_code = errno;

            log(LogLevel::Error,
                std::string("signalfd read failed: ") + std::strerror(error_code));
            return false;
        }

        log(LogLevel::Error,
            "signalfd read returned an unexpected size");

        return false;

    }
}


} // namespace