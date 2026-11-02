#include "mini_http/reactor/connection.h"
#include "mini_http/http/http_request.h"
#include "mini_http/reactor/event_loop.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cerrno>


namespace {

using mini_http::Connection;
using mini_http::ConnectionState;
using mini_http::HttpRequest;
using mini_http::ReadResult;
using mini_http::EventLoop;
using Clock = std::chrono::steady_clock;


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

void test_idle_timeout() {
    struct IdleCase {
        const char* name;
        std::chrono::milliseconds elapsed;
        bool expected;
    };

    const IdleCase cases[] = {
        {"just created", std::chrono::milliseconds(0), false},
        {"before deadline", std::chrono::milliseconds(29999), false},
        {"at deadline", std::chrono::milliseconds(30000), true},
        {"after deadline", std::chrono::milliseconds(30001), true}
    };

    for(const IdleCase& test : cases) {
        int sockets[2]{-1, -1};

        expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            test.name, "socketpair failed");

        Connection receiver(sockets[0], 1);
        Connection peer(sockets[1], 2);

        const Clock::time_point base{};
        const std::chrono::seconds timeout{30};

        receiver.state().io_idle_since = base;

        expect(receiver.idle_timed_out(base + test.elapsed, timeout) == test.expected,
                test.name, "idle timeout result mismatch");

        receiver.state().request_bytes =
            "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

        EventLoop event_loop;
        expect(event_loop.fd() != -1,
                test.name, "event_loop create false fd");

        HttpRequest request;
        const auto action = receiver.handle_event(event_loop, request);

        expect(action == mini_http::ConnectionAction::RequestReady,
                test.name, "request did not become ready");

        expect(receiver.processing(),
                test.name, "processing flag was not set");

        expect(!receiver.idle_timed_out(
                base + std::chrono::seconds{60}, timeout),
                test.name, "processing connection timed out");
    }
}

void test_io_activity() {
    int sockets[2]{-1, -1};

    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
        "io activity", "socketpair failed");

    Connection receiver(sockets[0], 1);
    Connection peer(sockets[1], 2);

    EventLoop event_loop;
    expect(event_loop.fd() != -1,
        "io activity", "event_loop create false fd");

    expect(event_loop.add_fd(receiver.fd(), EPOLLIN),
        "IO setup", "failed to register receiver");

    const auto old_time =
        Clock::now() - std::chrono::seconds{60};

    receiver.state().io_idle_since = old_time;

    HttpRequest request;

    const auto empty_result = receiver.read_request(request);

    expect(empty_result == ReadResult::WouldBlock,
        "empty read", "expected WouldBlock");

    expect(receiver.state().io_idle_since == old_time,
        "empty read", "idle time changed without data");

    const std::string prefix =
        "GET / HTTP/1.1\r\nHost: local";

    const ssize_t sent = send(
        peer.fd(), prefix.data(), prefix.size(),
        MSG_DONTWAIT | MSG_NOSIGNAL);

    expect(sent == static_cast<ssize_t>(prefix.size()),
        "partial read", "failed to send prefix");

    const auto before_read = Clock::now();
    const auto partial_result = receiver.read_request(request);
    const auto after_read = Clock::now();

    expect(partial_result == ReadResult::WouldBlock,
        "partial read", "expected WouldBlock");

    expect(receiver.state().io_idle_since >= before_read &&
        receiver.state().io_idle_since <= after_read,
        "partial read", "idle time was not refreshed");

    receiver.state().request_bytes =
        "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

    const auto action =
        receiver.handle_event(event_loop, request);

    expect(action == mini_http::ConnectionAction::RequestReady,
        "response start", "request did not become ready");
    expect(receiver.processing(),
        "response start", "processing flag was not set");

    receiver.state().io_idle_since = old_time;

    const auto before_start = Clock::now();
    const bool started =
        receiver.start_response(event_loop, "hello", true);
    const auto after_start = Clock::now();

    expect(started,
        "response start", "failed to start response");
    expect(!receiver.processing(),
        "response start", "processing flag was not cleared");
    expect(receiver.state().io_idle_since >= before_start &&
        receiver.state().io_idle_since <= after_start,
        "response start", "idle time was not reset");

    receiver.state().io_idle_since = old_time;

    const auto before_write = Clock::now();
    const auto write_result = receiver.flush_response();
    const auto after_write = Clock::now();

    expect(write_result == mini_http::WriteResult::Complete,
        "write progress", "response was not fully sent");
    expect(receiver.state().sent_bytes == 5,
        "write progress", "sent byte count mismatch");
    expect(receiver.state().io_idle_since >= before_write &&
        receiver.state().io_idle_since <= after_write,
        "write progress", "idle time was not refreshed");

    // 验证实际收到的字节，并排空队列后再构造发送堵塞。
    char response_bytes[5]{};
    const ssize_t received = recv(
        peer.fd(), response_bytes, sizeof(response_bytes), MSG_DONTWAIT);

    expect(received == static_cast<ssize_t>(sizeof(response_bytes)),
        "write progress", "peer did not receive the complete response");
    expect(std::string(response_bytes, sizeof(response_bytes)) == "hello",
        "write progress", "response bytes mismatch");

    const int send_buffer_size = 4096;
    expect(setsockopt(
        receiver.fd(), SOL_SOCKET, SO_SNDBUF,
        &send_buffer_size, sizeof(send_buffer_size)) == 0,
        "blocked write", "failed to set send buffer size");

    const std::string fill_bytes(4096, 'x');
    constexpr int max_fill_attempts = 256;
    bool send_buffer_full = false;

    // 对端不再读取；发送必须非阻塞且限次，避免测试挂住。
    for(int attempt = 0; attempt < max_fill_attempts; ++attempt) {
        const ssize_t written = send(
            receiver.fd(), fill_bytes.data(), fill_bytes.size(),
            MSG_DONTWAIT | MSG_NOSIGNAL);

        if(written > 0) {
            continue;
        }

        if(written < 0) {
            const int error_code = errno;

            if(error_code == EINTR) {
                continue;
            }

            if(error_code == EAGAIN || error_code == EWOULDBLOCK) {
                send_buffer_full = true;
                break;
            }
        }

        expect(false,
            "blocked write", "unexpected send result while filling buffer");
    }

    expect(send_buffer_full,
        "blocked write", "send buffer did not become blocked within limit");

    // 使用与堵塞探测相同长度的数据，避免较小写入仍能利用剩余空间。
    receiver.set_response(fill_bytes);
    receiver.state().io_idle_since = old_time;

    const auto blocked_result = receiver.flush_response();

    expect(blocked_result == mini_http::WriteResult::WouldBlock,
        "blocked write", "expected WouldBlock");
    expect(receiver.state().sent_bytes == 0,
        "blocked write", "send made unexpected progress");
    expect(receiver.state().io_idle_since == old_time,
        "blocked write", "idle time changed without send progress");
    expect(receiver.idle_timed_out(
        Clock::now(), std::chrono::seconds{2}),
        "blocked write", "stalled connection was not eligible for timeout");
}

void test_shutdown_state() {
    struct ShutdownCase {
        const char* name;
        std::string request_bytes;
        bool prepare_processing;
        bool prepare_response;
        std::size_t sent_prefix;
        mini_http::ConnectionAction expected_action;
    };

    const std::string complete_request =
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const std::string next_request =
        "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const std::string response_bytes = "hello";

    const ShutdownCase cases[] = {
        {"shutdown idle", "", false, false, 0,
            mini_http::ConnectionAction::Close},
        {"shutdown partial request", "GET / HTTP/1.1\r\nHost: local",
            false, false, 0, mini_http::ConnectionAction::Close},
        {"shutdown processing", complete_request, true, false, 0,
            mini_http::ConnectionAction::Keep},
        {"shutdown pending response", next_request, false, true, 0,
            mini_http::ConnectionAction::Keep},
        {"shutdown partially sent response", next_request, false, true, 2,
            mini_http::ConnectionAction::Keep}
    };

    for(const ShutdownCase& test : cases) {
        int sockets[2]{-1, -1};
        expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
                test.name, "socketpair failed");

        Connection receiver(sockets[0], 1);
        Connection peer(sockets[1], 2);
        EventLoop event_loop;

        expect(event_loop.fd() != -1,
                test.name, "event loop initialization failed");
        expect(event_loop.add_fd(receiver.fd(), EPOLLIN),
                test.name, "failed to register receiver");

        receiver.state().request_bytes = test.request_bytes;
        HttpRequest request;

        if(test.prepare_processing) {
            const auto action = receiver.handle_event(event_loop, request);
            expect(action == mini_http::ConnectionAction::RequestReady,
                    test.name, "request did not become ready");
            expect(receiver.processing(),
                    test.name, "processing flag was not set");
        }

        if(test.prepare_response) {
            // 先建立可复用响应，再检查begin_shutdown会禁止后续复用。
            expect(receiver.start_response(event_loop, response_bytes, true),
                    test.name, "failed to start response");

            if(test.sent_prefix != 0) {
                const ssize_t sent = send(
                    receiver.fd(), response_bytes.data(), test.sent_prefix,
                    MSG_DONTWAIT | MSG_NOSIGNAL);
                expect(sent == static_cast<ssize_t>(test.sent_prefix),
                        test.name, "failed to send response prefix");

                // 对应前面真实发出的前缀，不把未发送数据伪造成已发送。
                receiver.state().sent_bytes = test.sent_prefix;
            }
        }

        const ConnectionState before = receiver.state();
        const bool was_processing = receiver.processing();
        const int original_fd = receiver.fd();
        const std::uint64_t original_id = receiver.id();
        const auto shutdown_action = receiver.begin_shutdown();

        expect(shutdown_action == test.expected_action,
                test.name, "shutdown action mismatch");
        expect(receiver.begin_shutdown() == test.expected_action,
                test.name, "repeated shutdown changed the action");
        expect(receiver.fd() == original_fd && receiver.id() == original_id,
                test.name, "shutdown changed connection identity");
        expect(fcntl(receiver.fd(), F_GETFD) != -1,
                test.name, "shutdown closed a descriptor owned by Server");
        expect(receiver.processing() == was_processing,
                test.name, "shutdown changed processing state");
        expect(receiver.state().request_bytes == before.request_bytes,
                test.name, "shutdown changed buffered request bytes");
        expect(receiver.state().response_bytes == before.response_bytes,
                test.name, "shutdown changed serialized response bytes");
        expect(receiver.state().sent_bytes == before.sent_bytes,
                test.name, "shutdown reset send progress");
        expect(receiver.state().io_idle_since == before.io_idle_since,
                test.name, "shutdown changed the IO activity time");

        if(test.prepare_processing) {
            expect(receiver.handle_event(event_loop, request) ==
                    mini_http::ConnectionAction::Keep,
                    test.name, "processing work was not preserved");
        }

        if(test.prepare_response) {
            const auto completed_action =
                receiver.handle_event(event_loop, request);

            expect(completed_action == mini_http::ConnectionAction::Close,
                    test.name, "completed response did not close");
            expect(receiver.state().sent_bytes == response_bytes.size(),
                    test.name, "response was not completely sent");
            expect(receiver.state().request_bytes == next_request,
                    test.name, "shutdown consumed the next buffered request");
            expect(!receiver.processing(),
                    test.name, "shutdown started processing another request");

            std::string received_bytes;
            while(received_bytes.size() < response_bytes.size()) {
                char buffer[16]{};
                const ssize_t received = recv(
                    peer.fd(), buffer, sizeof(buffer), MSG_DONTWAIT);

                if(received < 0 && errno == EINTR) {
                    continue;
                }

                expect(received > 0,
                        test.name, "peer did not receive the complete response");
                received_bytes.append(buffer, static_cast<std::size_t>(received));
            }

            expect(received_bytes == response_bytes,
                    test.name, "response bytes were lost, duplicated or changed");
        }
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
        {"bad buffered requests", test_bad_buffered_request},
        {"idle timeout", test_idle_timeout},
        {"IO activity", test_io_activity},
        {"shutdown state", test_shutdown_state}
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

    std::cout << "All 6 connection test groups passed\n";

    return 0;
}