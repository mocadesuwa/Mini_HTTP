#include "mini_http/runtime/logger.h"

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>
#include <string>

namespace {

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_log_format() {
    struct LogCase {
        const char* name;
        mini_http::LogLevel level;
        std::string_view message;
        std::string_view expected;
    };

    const LogCase cases[] = {
        {"info", mini_http::LogLevel::Info,
             "hello", "[INFO] hello\n"},
        {"warning", mini_http::LogLevel::Warning,
             "hello", "[WARN] hello\n"},
        {"error", mini_http::LogLevel::Error,
             "hello", "[ERROR] hello\n"},
        {"empty info", mini_http::LogLevel::Info,
             "", "[INFO] \n"}
    };

    for(const LogCase& test : cases) {
        std::ostringstream output;

        std::streambuf* original_buffer =
            std::cerr.rdbuf(output.rdbuf());

        mini_http::log(test.level, test.message);

        std::cerr.rdbuf(original_buffer);

        expect(output.str() == test.expected,
                test.name, "log mismatch");
    }
}

void test_concurrent_logging() {
    const std::size_t thread_count = 4;
    const std::size_t message_per_thread = 20;

    std::unordered_set<std::string> expected_line;
    std::vector<std::thread> workers;

    for(std::size_t worker_index = 0; worker_index < thread_count; worker_index++) {
        for(std::size_t message_index = 0; message_index < message_per_thread; message_index++) {
            expected_line.insert
            ("[INFO] worker=" + std::to_string(worker_index) +
                " message=" + std::to_string(message_index));
        }
    }

    std::ostringstream output;

    std::streambuf* original_buffer =
        std::cerr.rdbuf(output.rdbuf());

    for(std::size_t worker_index = 0; worker_index < thread_count; worker_index++) {
        workers.emplace_back([worker_index] {
            for(std::size_t message_index = 0; message_index < message_per_thread; message_index++) {
                const std::string message =
                    "worker=" + std::to_string(worker_index) +
                    " message=" + std::to_string(message_index);

                mini_http::log(mini_http::LogLevel::Info, message);
            }
        });
    }

    for(std::thread& worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
    std::cerr.rdbuf(original_buffer);

    std::istringstream input(output.str());

    std::string line;
    std::size_t line_count = 0;

    while(std::getline(input, line)) {
        ++line_count;

        const std::size_t removed = expected_line.erase(line);

        expect(removed == 1,
                "removed", "removed unequal 1");
    }

    expect(line_count == thread_count * message_per_thread,
            "line count", "line count unequal expected");

    expect(expected_line.empty(),
            "expected line", "expected line is not empty");
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"log format", test_log_format},
        {"concurrent logging", test_concurrent_logging}
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

    std::cout << "All 2 log test pass\n";

    return 0;
}