#include "mini_http/connection.h"
#include "mini_http/http_parser.h"
#include "mini_http/event_loop.h"
#include "mini_http/http_handler.h"
#include "mini_http/http_response.h"

#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>
#include <cstdio>

namespace mini_http {

ReadResult read_request(
    int client_fd, ConnectionState &connection, HttpRequest &request) {

    const HttpParser parser;

    std::string& request_bytes = connection.request_bytes;

    std::size_t consumed_bytes = 0;

    char buffer[4096];

    while (true) {
        ssize_t n = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            MSG_DONTWAIT);

        if(n > 0) {
            request_bytes.append(buffer, static_cast<std::size_t>(n));

            ParseResult result = parser.parse_request(
                    request_bytes, request, consumed_bytes);

            if(result == ParseResult::Complete) {
                return ReadResult::Complete;
            } else if(result == ParseResult::NeedMore) {
                continue;
            } else{
                return ReadResult::BadRequest;
            }
        }

        if(n == 0) {
            if(request_bytes.empty()) {
                return ReadResult::PeerClosed;
            }

            return ReadResult::BadRequest;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadResult::WouldBlock;
        }

        const int error_code = errno;
        std::cerr << "recv failed: "
                  << std::strerror(error_code) << '\n';

        return ReadResult::IoError;
    }

}

WriteResult flush_response(
    int client_fd, ConnectionState &connection) {

    while(connection.sent_bytes < connection.response_bytes.size()) {
        const ssize_t n = send(
            client_fd,
            connection.response_bytes.data() + connection.sent_bytes,
            connection.response_bytes.size() - connection.sent_bytes,
            MSG_NOSIGNAL | MSG_DONTWAIT);

        if(n > 0) {
            connection.sent_bytes +=
                static_cast<std::size_t>(n);
            continue;
        }

        if(n < 0) {
            if(errno == EINTR) {
                continue;
            }

            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return WriteResult::WouldBlock;
            }
        }
        return WriteResult::IoError;
    }

    return WriteResult::Complete;
}


Connection::Connection(int client_fd, std::uint64_t id)
    : client_fd_(client_fd), id_(id) {
}

Connection::~Connection() {
    if(client_fd_ >= 0) {
        close(client_fd_);
    }
}

int Connection::fd() const noexcept {
    return client_fd_;
}

std::uint64_t Connection::id() const noexcept {
    return id_;
}

bool Connection::processing() const noexcept {
    return processing_;
}

ConnectionState& Connection::state() noexcept {
    return state_;
}

ReadResult Connection::read_request(HttpRequest& request) {
    return mini_http::read_request(client_fd_, state_, request);
}

WriteResult Connection::flush_response() {
    return mini_http::flush_response(client_fd_, state_);
}

bool Connection::has_response() const noexcept {
    return !state_.response_bytes.empty();
}

void Connection::set_response(std::string bytes) {
    state_.response_bytes = std::move(bytes);
    state_.sent_bytes = 0;
}

namespace  {

bool watch_writable(EventLoop& event_loop, int client_fd) {
    if(!event_loop.modify_fd(client_fd, EPOLLOUT)) {
        std::perror("error_ctl MOD");
        return false;
    }

    return true;
}

} // inner namespace

bool Connection::start_response(EventLoop& event_loop, std::string bytes) {
    set_response(std::move(bytes));

    // 业务完成，连接开始准备发送
    processing_ = false;
    return watch_writable(event_loop, client_fd_);
}

ConnectionAction Connection::handle_event(EventLoop& event_loop, HttpRequest& request) {
    if(processing_) {
        return ConnectionAction::Keep;
    }

    if(has_response()) {
        const WriteResult write_result =
            flush_response();

        if(write_result == WriteResult::WouldBlock) {
            return ConnectionAction::Keep;
        }

        if(write_result == WriteResult::IoError) {
            std::cerr << "Failed to send response\n";
        }
    } else {

        const ReadResult result = read_request(request);

        switch(result) {
            case ReadResult::Complete: {
                processing_ = true;
                return ConnectionAction::RequestReady;
            }

            case ReadResult::WouldBlock:
                return ConnectionAction::Keep;

            case ReadResult::PeerClosed:
                break;

            case ReadResult::BadRequest: {
                HttpResponse response;
                response.status_code = 400;
                response.body = "Bad request\n";

                if(start_response(event_loop, response.serialize())) {
                    return ConnectionAction::Keep;
                }
                break;
            }

            case ReadResult::IoError:
                std::cerr << "Failed to read request\n";
                break;
        }
    }

    return ConnectionAction::Close;
}

} // namespace