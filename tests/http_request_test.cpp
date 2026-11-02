#include "mini_http/http/http_request.h"

#include <stdexcept>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mini_http::HttpRequest;
using mini_http::HttpHeader;

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_keep_alive() {

    struct KeepAliveCase {
        const char* name;
        std::vector<mini_http::HttpHeader> headers;
        bool expected;
    };

    const KeepAliveCase cases[] = {
        {"no connection", {}, true},
        {"explicit keep alive",
            {{"connection", "keep-alive"}}, true},
        {"explicit close",
            {{"connection", "close"}}, false},
        {"mixed case close",
            {{"connection", "ClOsE"}}, false},
        {"close at end of list",
            {{"connection", "keep-alive, close"}}, false},
        {"close at start of list",
            {{"connection", "close, keep-alive"}}, false},
        {"close surrounded by whitespace",
            {{"connection", "keep-alive, \tClOsE \t, keep-alive"}}, false},
        {"whitespace without close",
            {{"connection", "keep-alive, \tkeep-alive\t , keep-alive"}}, true},
        {"close in second header",
            {
                {"connection", "keep-alive"},
                {"connection", "close"}
            }, false},
        {"close in first header",
            {
                {"connection", "close"},
                {"connection", "keep-alive"}
            }, false},
        {"close substring", {{"connection", "x-close"}}, true},
        {"unrelated header", {{"x-test", "close"}}, true}
    };

    for(const KeepAliveCase& test : cases) {
        HttpRequest request{};

        request.method = "GET";
        request.target = "/";
        request.version = "HTTP/1.1";
        request.headers = test.headers;

        const bool actual = mini_http::should_keep_alive(request);

        expect(actual == test.expected, test.name, "keep-alive decision mismatch");
    }

}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"keep alive", test_keep_alive}
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

    return 0;
}