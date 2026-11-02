#pragma once

namespace mini_http {

class Acceptor {
public:
    explicit Acceptor(int listen_fd) noexcept;
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    int fd();
    int accept_one();

    void stop() noexcept;

private:
    int listen_fd_{-1};
};

} // namespace