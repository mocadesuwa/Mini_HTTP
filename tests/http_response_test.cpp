#include "mini_http/http/http_response.h"
#include <stdexcept>
#include <string_view>
#include <string>
#include <iostream>
#include <exception>

namespace {

using mini_http::HttpResponse;


void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_response_format() {
    const HttpResponse response {
        200,
        "text/plain; charset=utf-8",
        "Hello, HTTP!\n",
        {}
    };

    const std::string actual = response.serialize();

    const std::string expected =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 13\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello, HTTP!\n";

    expect(
        actual == expected,
        "complete response", "serialized response mismatch");
}

void test_status_line() {
    struct StatusCase {
        int status_code;
        std::string_view expected_line;
    };

    const StatusCase cases[] = {
        {200, "HTTP/1.1 200 OK\r\n"},
        {400, "HTTP/1.1 400 Bad Request\r\n"},
        {403, "HTTP/1.1 403 Forbidden\r\n"},
        {404, "HTTP/1.1 404 Not Found\r\n"},
        {405, "HTTP/1.1 405 Method Not Allowed\r\n"},
        {500, "HTTP/1.1 500 Internal Server Error\r\n"}
    };

    for(const StatusCase& test : cases) {
        HttpResponse response;
        response.status_code = test.status_code;

        const std::string actual = response.serialize();

        expect(actual.compare(
                0, test.expected_line.size(), test.expected_line) == 0,
            test.expected_line, "status line mismatch");

    }
}

void test_binary_body() {
    HttpResponse response;

    response.body = std::string("a\0b\r\n", 5);

    const std::string actual = response.serialize();

    const std::size_t separator = actual.find("\r\n\r\n");

    expect(
        separator != std::string::npos,
        "binary body", "header/body separator missing");

    const std::size_t body_start = separator + 4;

    const std::string headers = actual.substr(0, body_start);

    const std::string expected_header = "\r\nContent-Length: 5\r\n";
    expect(
        headers.find(expected_header) != std::string::npos,
        "binary body", "Content-Length should be 5");

    const std::string actual_body = actual.substr(body_start);

    expect(
        actual_body == response.body,
        "binary body", "body bytes differ from expected");
}

void test_extra_headers() {
    HttpResponse response;

    response.status_code = 405;
    response.body = "Method not allowed\n";

    response.extra_headers.emplace_back("Allow", "GET");
    response.extra_headers.emplace_back("X-Test", "yes");

    const std::string actual = response.serialize();
    const std::size_t separator = actual.find("\r\n\r\n");

    expect(
        separator != std::string::npos,
        "extra headers", "header/body separator missing");

    const std::string headers = actual.substr(0, separator + 4);

    expect(headers.find("\r\nAllow: GET\r\n") != std::string::npos,
            "extra headers", "Allow header missing or malformed");

    expect(headers.find("\r\nX-Test: yes\r\n") != std::string::npos,
            "extra headers", "X-Test header missing or malformed");
}

void test_empty_body() {
    const HttpResponse response;

    const std::string actual = response.serialize();

    const std::size_t separator = actual.find("\r\n\r\n");

    expect(
        separator != std::string::npos,
        "empty body", "header/body separator missing");

    const std::string headers = actual.substr(0, separator + 4);

    expect(headers.find("\r\nContent-Length: 0\r\n") !=  std::string::npos,
            "empty body", "Content-Length shoude be 0");

    expect(separator + 4 == actual.size(),
            "empty body", "unexpected bytes after header/boyd separator");
}

void test_unsupported_status() {
    HttpResponse response;
    response.status_code = 501;

    bool caught = false;

    try{
        (void)response.serialize();
    } catch(const std::invalid_argument&) {
        caught = true;
    }

    expect(caught == true,
            "unsupported status",
            "expected std::invalid_argument waws not thrown");
}

void test_connection_header() {
    struct ConnectionCase {
        const char* name;
        bool keep_alive;
        std::string body;
        std::string_view expected_connection;
        std::size_t expected_length;
    };

    const ConnectionCase cases[] = {
        {"explicit close", false,
            "hello","Connection: close\r\n", 5},
        {"keep alive", true,
            "hello", "Connection: keep-alive\r\n", 5},
        {"empty body", true,
            "", "Connection: keep-alive\r\n", 0},
        {"binary body", true,
            std::string("a\0b\r\n", 5), "Connection: keep-alive\r\n", 5}
    };

    for(const ConnectionCase& test : cases) {
        HttpResponse response;
        response.keep_alive = test.keep_alive;
        response.body = test.body;
        response.extra_headers.emplace_back("X-Test", "yes");

        const std::string actual = response.serialize();
        const std::size_t separator = actual.find("\r\n\r\n");

        expect(separator != std::string::npos,
            test.name, "cannot find separator");

        const std::string headers = actual.substr(0, separator + 4);
        const std::string actual_body = actual.substr(separator + 4);

        const std::size_t pos = headers.find("\r\nConnection: ");
        expect(pos != std::string::npos,
            test.name, "cannot find Connection");

        expect(headers.find("\r\nConnection: ", pos + 1) == std::string::npos,
            test.name, "Connection appear twice");

        const std::string expected_length_header =
            "\r\nContent-Length: " +
            std::to_string(test.expected_length) +
            "\r\n";

        expect(headers.find(expected_length_header) != std::string::npos,
            test.name, "cannot find correct content-length");

        const std::string expected_connection_header =
            "\r\n" + std::string(test.expected_connection);

        expect(headers.find(expected_connection_header) != std::string::npos,
            test.name, "connection header mismatch");

        expect(actual_body == test.body,
            test.name, "body dismatch");

        expect(headers.find("\r\nX-Test: yes\r\n") != std::string::npos,
            test.name, "X-Test header missing or malformed");
    }
}


} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"response format", test_response_format},
        {"status line", test_status_line},
        {"binary body", test_binary_body},
        {"extra headers", test_extra_headers},
        {"empty body", test_empty_body},
        {"unsupported status", test_unsupported_status},
        {"connection header", test_connection_header}
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

    std::cout << "All 7 HTTP response test groups pass\n";


    return 0;
}