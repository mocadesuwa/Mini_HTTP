#include "mini_http/http_handler.h"
#include <stdexcept>
#include <exception>
#include <string>
#include <string_view>
#include <iostream>

namespace {

using mini_http::HttpRequest;
using mini_http::HttpResponse;
using mini_http::handle_request;

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_get_root() {
    HttpRequest request = {
        "GET", "/", "HTTP/1.1",
        {}, ""
    };


    const HttpResponse response = handle_request(request);

    expect(response.status_code == 200,
            "get root", "expected status 200");
    expect(response.body == "Hello, HTTP!\n",
            "get root", "root response body mismatch");
}

void test_get_not_found() {
    HttpRequest request = {
        "GET", "/missing", "HTTP/1.1",
        {}, ""
    };

    const HttpResponse response = handle_request(request);

    expect(response.status_code == 404,
        "get not found", "expected status 404");

    expect(response.body == "Not found\n",
        "get not found", "not-found response body mismatch");
}


void test_method_not_allowed() {
    const HttpRequest cases[] = {
        {"POST", "/", "HTTP/1.1",
            {}, ""},
        {"DELETE", "/", "HTTP/1.1",
            {}, ""},
            {"POST", "/missinh", "HTTP/1.1",
            {}, ""}
    };

    for(const HttpRequest& request : cases) {
        const std::string case_name = request.method + " " + request.target;

        const HttpResponse response = handle_request(request);

        expect(response.status_code == 405,
            case_name, "expected status 405");

        expect(response.body == "Method not allowed\n",
            case_name, "wrong body");

        expect(response.extra_headers.size() == 1,
            case_name, "expected exactly one extra header");

        const auto& header = response.extra_headers.front();

        expect(header.first == "Allow",
            case_name, "expected Allow header");

        expect(header.second == "GET",
            case_name, "Allow value must be GET");
    }
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"get root", test_get_root},
        {"get not found", test_get_not_found},
        {"method not allowed", test_method_not_allowed}
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

    std::cout << "All 3 HTTP handler test groups pass\n";

    return 0;
}