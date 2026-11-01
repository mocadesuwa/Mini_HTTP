#include "mini_http/config.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <cstdint>

namespace {

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_parse_success() {
    struct SuccessCase {
        const char* name;
        std::vector<const char*> arguments;
        mini_http::ServerConfig expected;
    };

    const std::string maximum_idle =
        std::to_string(std::chrono::seconds::max().count());

    const SuccessCase cases[] = {
        {
            "defaults",
            {"mini_http_server"},
            {8081, "www", 2}
        },
        {
            "port override",
            {"mini_http_server", "--port", "8082"},
            {8082, "www", 2}
        },
        {
            "root override",
            {"mini_http_server", "--root", "assets"},
            {8081, "assets", 2}
        },
        {
            "threads override",
            {"mini_http_server", "--threads", "4"},
            {8081, "www", 4}
        },
        {
            "all options reordered",
            {
                "mini_http_server", "--threads", "4",
                "--root", "assets", "--idle-timeout", "5",
                "--port", "8082"
            },
            {8082, "assets", 4, std::chrono::seconds{5}}
        },
        {
            "minimum port",
            {"mini_http_server", "--port", "1"},
            {1, "www", 2}
        },
        {
            "maximum port",
            {"mini_http_server", "--port", "65535"},
            {65535, "www", 2}
        },
        {
            "minimum threads",
            {"mini_http_server", "--threads", "1"},
            {8081, "www", 1}
        },
        {
            "idle timeout override",
            {"mini_http_server", "--idle-timeout", "2"},
            {8081, "www", 2, std::chrono::seconds{2}}
        },
        {
            "minimum idle timeout",
            {"mini_http_server", "--idle-timeout", "1"},
            {8081, "www", 2, std::chrono::seconds{1}}
        },
        {
            "maximum idle timeout",
            {"mini_http_server", "--idle-timeout", maximum_idle.c_str()},
            {8081, "www", 2, std::chrono::seconds::max()}
        }
    };

    for(const SuccessCase& test : cases) {
        mini_http::ServerConfig config{
            9000, "original-root",
            7, std::chrono::seconds{77}};
        std::string error = "old error";

        mini_http::ConfigParseResult result =
            mini_http::parse_arguments(
                static_cast<int>(test.arguments.size()),
                test.arguments.data(), config, error);

        expect(result == mini_http::ConfigParseResult::Success,
                test.name, "parse result mismatch");
        expect(config.port == test.expected.port,
                test.name, "port mismatch");
        expect(config.document_root == test.expected.document_root,
                test.name, "document root mismatch");
        expect(config.thread_count == test.expected.thread_count,
                test.name, "thread count mismatch");
        expect(error.empty(),
                test.name, "error was not cleared");
        expect(config.idle_timeout == test.expected.idle_timeout,
                test.name, "idle timeout mismatch");
    }
}

void test_parse_help() {
    struct HelpCase {
        const char* name;
        std::vector<const char*> arguments;
    };

    const HelpCase cases[] = {
        {
            "help only",
            {"mini_http_server", "--help"}
        },
        {
            "help after port override",
            {"mini_http_server", "--port", "8082", "--help"}
        },
        {
            "help after idle timeout",
            {"mini_http_server", "--idle-timeout", "2", "--help"}
        }
    };

    for(const HelpCase& test : cases) {
        const mini_http::ServerConfig original{
            9000, "original-root", 7, std::chrono::seconds{77}
        };
        mini_http::ServerConfig config = original;
        std::string error = "old error";

        mini_http::ConfigParseResult result =
            mini_http::parse_arguments(
                static_cast<int>(test.arguments.size()),
                test.arguments.data(), config, error);

        expect(result == mini_http::ConfigParseResult::Help,
                test.name, "parse result mismatch");
        expect(config.port == original.port,
                test.name, "port changed on help");
        expect(config.document_root == original.document_root,
                test.name, "document root changed on help");
        expect(config.thread_count == original.thread_count,
                test.name, "thread count changed on help");
        expect(config.idle_timeout == original.idle_timeout,
                test.name, "idle timeout changed on help");
        expect(error.empty(),
                test.name, "error was not cleared");
    };
}

void test_parse_error() {
    const char* overflow_number =
        "9999999999999999999999999999999999999999";

    // 先转为无符号类型，避免在秒数最大值上直接加一溢出。
    const std::string too_large_idle =
        std::to_string(
            static_cast<std::uintmax_t>(
                std::chrono::seconds::max().count()) + 1);

    struct ErrorCase {
        const char* name;
        std::vector<const char*> arguments;
        const char* expected_option;
    };

    const ErrorCase cases[] = {
        {
            "unknown option",
            {"mini_http_server", "--unknown"},
            "--unknown"
        },
        {
            "missing port",
            {"mini_http_server", "--port"},
            "--port"
        },
        {
            "missing root",
            {"mini_http_server", "--root"},
            "--root"
        },
        {
            "missing threads",
            {"mini_http_server", "--threads"},
            "--threads"
        },
        {
            "empty port",
            {"mini_http_server", "--port", ""},
            "--port"
        },
        {
            "zero port",
            {"mini_http_server", "--port", "0"},
            "--port"
        },
        {
            "negative port",
            {"mini_http_server", "--port", "-1"},
            "--port"
        },
        {
            "non-numeric port",
            {"mini_http_server", "--port", "abc"},
            "--port"
        },
        {
            "trailing port characters",
            {"mini_http_server", "--port", "8082x"},
            "--port"
        },
        {
            "port out of range",
            {"mini_http_server", "--port", "65536"},
            "--port"
        },
        {
            "port integer overflow",
            {"mini_http_server", "--port", overflow_number},
            "--port"
        },
        {
            "zero threads",
            {"mini_http_server", "--threads", "0"},
            "--threads"
        },
        {
            "threads integer overflow",
            {"mini_http_server", "--threads", overflow_number},
            "--threads"
        },
        {
            "empty root",
            {"mini_http_server", "--root", ""},
            "--root"
        },
        {
            "error after valid option",
            {"mini_http_server", "--port", "8082", "--threads", "0"},
            "--threads"
        },
        {
            "missing idle timeout",
            {"mini_http_server", "--idle-timeout"},
            "--idle-timeout"
        },
        {
            "zero idle timeout",
            {"mini_http_server", "--idle-timeout", "0"},
            "--idle-timeout"
        },
        {
            "invalid idle timeout suffix",
            {"mini_http_server", "--idle-timeout", "2s"},
            "--idle-timeout"
        },
        {
            "idle timeout out of range",
            {"mini_http_server", "--idle-timeout", too_large_idle.c_str()},
            "--idle-timeout"
        },
        {
            "error after idle timeout",
            {"mini_http_server", "--idle-timeout", "2", "--port", "0"},
            "--port"
        }
    };

    for(const ErrorCase& test : cases) {
        const mini_http::ServerConfig original{
            9000, "original-root", 7, std::chrono::seconds{77}
        };
        mini_http::ServerConfig config = original;
        std::string error = "old error";

        mini_http::ConfigParseResult result =
            mini_http::parse_arguments(
                static_cast<int>(test.arguments.size()),
                test.arguments.data(), config, error);

        expect(result == mini_http::ConfigParseResult::Error,
                test.name, "parse result mismatch");
        expect(config.port == original.port,
                test.name, "port changed on error");
        expect(config.document_root == original.document_root,
                test.name, "document root changed on error");
        expect(config.thread_count == original.thread_count,
                test.name, "thread count changed on error");
        expect(config.idle_timeout == original.idle_timeout,
                test.name, "idle timeout changed on error");
        expect(error.find(test.expected_option) != std::string::npos,
                test.name, "error does not identify the option");
    }
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"parse success", test_parse_success},
        {"parse help", test_parse_help},
        {"parse errors", test_parse_error}
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

    std::cout << "All 3 config test groups passed\n";

    return 0;
}