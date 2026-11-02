#include "mini_http/shutdown_signal.h"

#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

sigset_t current_mask() {
    sigset_t mask{};
    const int result = pthread_sigmask(SIG_SETMASK, nullptr, &mask);

    if(result != 0) {
        throw std::system_error(
            result, std::generic_category(), "query signal mask");
    }

    return mask;
}

class SignalMaskGuard {
public:
    SignalMaskGuard() : saved_mask_(current_mask()) {
    }

    ~SignalMaskGuard() {
        // 断言抛异常时也恢复测试开始前的屏蔽字。
        pthread_sigmask(SIG_SETMASK, &saved_mask_, nullptr);
    }

    SignalMaskGuard(const SignalMaskGuard&) = delete;
    SignalMaskGuard& operator=(const SignalMaskGuard&) = delete;

private:
    sigset_t saved_mask_{};
};

bool masks_equal(const sigset_t& left, const sigset_t& right) {
    // 不比较sigset_t的对象字节，逐个比较Linux信号的成员资格。
    for(int signal_number = 1; signal_number < NSIG; ++signal_number) {
        if(sigismember(&left, signal_number) !=
            sigismember(&right, signal_number)) {
            return false;
        }
    }

    return true;
}

void test_signal_lifecycle() {
    const char* case_name = "signal lifecycle";
    SignalMaskGuard mask_guard;

    // 明确建立基线：两种停止信号未屏蔽，另保留一个已屏蔽的无关信号。
    sigset_t baseline = current_mask();
    sigdelset(&baseline, SIGINT);
    sigdelset(&baseline, SIGTERM);
    sigaddset(&baseline, SIGUSR1);

    const int setup_result =
        pthread_sigmask(SIG_SETMASK, &baseline, nullptr);
    expect(setup_result == 0, case_name, "test mask setup failed");

    sigset_t expected_mask = baseline;
    sigaddset(&expected_mask, SIGINT);
    sigaddset(&expected_mask, SIGTERM);

    int owned_fd = -1;

    {
        mini_http::ShutdownSignal shutdown_signal;
        owned_fd = shutdown_signal.fd();

        expect(owned_fd >= 0, case_name, "invalid signal fd");

        const int status_flags = fcntl(owned_fd, F_GETFL);
        expect(status_flags != -1, case_name, "F_GETFL failed");
        expect((status_flags & O_NONBLOCK) != 0,
                case_name, "signal fd is blocking");

        const int descriptor_flags = fcntl(owned_fd, F_GETFD);
        expect(descriptor_flags != -1, case_name, "F_GETFD failed");
        expect((descriptor_flags & FD_CLOEXEC) != 0,
                case_name, "signal fd lacks close-on-exec");
        expect(masks_equal(current_mask(), expected_mask),
                case_name, "constructor changed the mask incorrectly");

        bool received = true;
        const bool empty_succeeded = shutdown_signal.consume(received);

        expect(empty_succeeded && !received,
                case_name, "empty consume failed or reported a signal");

        struct SignalCase {
            const char* name;
            int signals[2];
            int count;
        };

        const SignalCase cases[] = {
            {"consume SIGINT", {SIGINT, 0}, 1},
            {"consume SIGTERM", {SIGTERM, 0}, 1},
            {"drain both signals", {SIGINT, SIGTERM}, 2}
        };

        for(const SignalCase& test : cases) {
            for(int index = 0; index < test.count; ++index) {
                // 只发给测试当前线程，不向服务器或其他进程发送信号。
                const int send_result =
                    pthread_kill(pthread_self(), test.signals[index]);
                expect(send_result == 0, test.name, "self signal failed");
            }

            received = false;
            const bool succeeded = shutdown_signal.consume(received);

            expect(succeeded && received,
                    test.name, "signal consume failed or did not report a signal");

            received = true;
            const bool drained = shutdown_signal.consume(received);
            expect(drained && !received,
                    test.name, "signal was not drained or received was not reset");
        }

        // 留一个待处理信号，检查析构消费后再恢复未屏蔽基线的路径。
        const int send_result = pthread_kill(pthread_self(), SIGTERM);
        expect(send_result == 0, case_name, "pending cleanup signal failed");
    }

    errno = 0;
    const int closed_flags = fcntl(owned_fd, F_GETFD);
    const int close_error = errno;
    expect(closed_flags == -1 && close_error == EBADF,
            case_name, "destructor did not close the signal fd");
    expect(masks_equal(current_mask(), baseline),
            case_name, "destructor did not restore the original mask");
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"signal lifecycle", test_signal_lifecycle}
    };

    for(const TestGroup& group : groups) {
        try {
            group.run();
            std::cout << "[PASS] " << group.name << '\n';
        } catch(const std::exception& error) {
            std::cerr << "[FAIL] " << group.name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "All 1 shutdown signal test group passed\n";

    return 0;
}
