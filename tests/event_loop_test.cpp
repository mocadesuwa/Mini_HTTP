#include "mini_http/reactor/event_loop.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_loop_stop() {
    struct StopCase {
        const char* name;
        int stop_after_ticks;
        int timeout_ms;
    };

    const StopCase cases[] = {
        {"immediate stop", 0, -1},
        {"stop after one tick", 1, 0},
        {"stop after three ticks", 3, 0}
    };

    for(const StopCase& test : cases) {
        mini_http::EventLoop event_loop;

        expect(event_loop.fd() != -1,
                test.name, "event_loop create false fd");

        int tick_count = 0;
        int handler_calls = 0;

        const int result = event_loop.loop(
            [&](int, std::uint32_t) {
                ++handler_calls;
                return true;
            },
            test.timeout_ms,
            [&] {
                ++tick_count;
            },
            [&] {
                return tick_count >= test.stop_after_ticks;
            });

        expect(result == 0,
                test.name, "loop result mismatch");
        expect(tick_count == test.stop_after_ticks,
                test.name, "tick count mismatch");
        expect(handler_calls == 0,
                test.name, "unexpected handler call");
    }

    const char* case_name = "stop after posted task";
    mini_http::EventLoop event_loop;

    expect(event_loop.fd() != -1,
            case_name, "event loop initialization failed");

    bool executed = false;
    int tick_count = 0;
    int handler_calls = 0;

    const bool posted = event_loop.post([&] {
        executed = true;
    });

    expect(posted, case_name, "post failed");

    const int result = event_loop.loop(
        [&](int, std::uint32_t) {
            ++handler_calls;
            return true;
        },
        0,
        [&] {
            ++tick_count;
        },
        [&] {
            return executed;
        });

    expect(result == 0,
            case_name, "loop result mismatch");
    expect(executed,
            case_name, "posted task was not executed");
    expect(tick_count == 1,
            case_name, "tick count mismatch");
    expect(handler_calls == 0,
            case_name, "unexpected handler call");
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"loop stop", test_loop_stop}
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

    std::cout << "All 1 event loop test group passed\n";

    return 0;
}
