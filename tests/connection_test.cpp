#include "mini_http/connection.h"
#include "mini_http/http_request.h"

#include <sys/socket.h>
#include <iostream>
#include <stdexcept>

namespace {

using mini_http::Connection;
using mini_http::ConnectionState;
using mini_http::HttpRequest;
using mini_http::ReadResult;

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_buffered_requests() {
    struct BufferedCase {
        const char* name;
        std::string first_bytes;
        std::string expected_method;
        std::string expected_target;
        std::string expected_body;
    };

    const BufferedCase cases[] = {
        {"normal GET",
            "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n",
            "GET", "/first", ""},
        {"binary POST",
            "POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\n" + std::string("a\0b", 3),
            "POST", "/submit", std::string("a\0b", 3)}
    };

    const std::string next_bytes =
        "GET /second HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    for(const BufferedCase& test : cases) {
        int sockets[2]{-1, -1};

        expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            test.name, "socketpair failed");

        Connection receiver(sockets[0], 1);
        Connection peer(sockets[1], 2);

        ConnectionState& state = receiver.state();
        state.request_bytes = test.first_bytes + next_bytes;

        HttpRequest first_request;

        const ReadResult first_result =
            receiver.read_request(first_request);

        expect(first_result == ReadResult::Complete,
                test.name, "result mismatch");
        expect(first_request.method == test.expected_method,
                test.name, "name mismatch");
        expect(first_request.target == test.expected_target,
                test.name, "target mismatch");
        expect(first_request.body == test.expected_body,
                test.name, "body mismtch");
        expect(state.request_bytes == next_bytes,
                test.name, "request bytes mismatch");

        HttpRequest second_request;

        const ReadResult second_result =
            receiver.read_request(second_request);

        expect(second_result == ReadResult::Complete,
                test.name, "result mismatch");
        expect(second_request.method == "GET",
                test.name, "method mismatch");
        expect(second_request.target == "/second",
                test.name, "target mismatch");
        expect(second_request.body == "",
                test.name, "body mismatch");
        expect(state.request_bytes.empty(),
                test.name, "request bytes not empty");
    }
}

void test_fragmented_requests() {
    struct FragmentCase {
        const char* name;
        std::string prefix;
        std::string suffix;
        std::string expected_method;
        std::string expected_target;
        std::string expected_body;
    };

    const FragmentCase cases[] = {
        {"normal GET",
        "GET /second HTTP/1.1\r\nHost: local",
        "host\r\n\r\n",
        "GET", "/second", ""},
        {"binary POST",
        "POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\na",
        std::string("\0b", 2),
        "POST", "/submit", std::string("a\0b", 3)}
    };

    const std::string first_bytes =
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n";

    for(const FragmentCase& test : cases) {
        int sockets[2]{-1, -1};

        expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            test.name, "socketpair failed");

        Connection receiver(sockets[0], 1);
        Connection peer(sockets[1], 2);

        ConnectionState& state = receiver.state();
        state.request_bytes = first_bytes + test.prefix;

        HttpRequest first_request;

        const ReadResult first_result =
            receiver.read_request(first_request);

        expect(first_result == ReadResult::Complete,
                test.name, "first result not complete");
        expect(first_request.target == "/first",
                test.name, "target mismatch");
        expect(state.request_bytes == test.prefix,
                test.name, "remaining buffer does not match prefix");
        HttpRequest second_request;

        const ReadResult second_result =
            receiver.read_request(second_request);

        expect(second_result == ReadResult::WouldBlock,
                test.name, "second result not wouldblock");
        expect(state.request_bytes == test.prefix,
                test.name, "remaining buffer does not match");

        const ssize_t written = send(
            peer.fd(), test.suffix.data(), test.suffix.size(), MSG_NOSIGNAL);

        expect(written == static_cast<ssize_t>(test.suffix.size()),
                test.name, "failed to send complete suffix");

        HttpRequest completed_request;

        const ReadResult completed_result =
            receiver.read_request(completed_request);

        expect(completed_result == ReadResult::Complete,
                test.name, "completed result not complete");
        expect(completed_request.method == test.expected_method,
                test.name, "name mismatch");
        expect(completed_request.target == test.expected_target,
                test.name, "target mismatch");
        expect(completed_request.body == test.expected_body,
                test.name, "body mismtch");
        expect(state.request_bytes == "",
                test.name, "remaining buffer not empty");
    }
}

void test_bad_buffered_request() {
    struct BadCase {
        const char* name;
        std::string bytes;
    };

    const BadCase cases[] = {
        {"unsupported VER",
             "GET /first HTTP/1.0\r\nHost: localhost\r\n\r\n"},
        {"invalid length",
             "POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n"}
    };

    for(const BadCase& test : cases) {
        int sockets[2]{-1, -1};

        expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            test.name, "socketpair failed");

        Connection receiver(sockets[0], 1);
        Connection peer(sockets[1], 2);

        ConnectionState& state = receiver.state();
        state.request_bytes = test.bytes;

        HttpRequest request;
        const ReadResult result =
            receiver.read_request(request);

        expect(result == ReadResult::BadRequest,
                test.name, "invalid bufferd request was not rejected");
    }
}


} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"bufferd request", test_buffered_requests},
        {"fragmented requests", test_fragmented_requests},
        {"bad buffered requests", test_bad_buffered_request}
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

    std::cout << "All 3 connection test pass\n";

    return 0;
}