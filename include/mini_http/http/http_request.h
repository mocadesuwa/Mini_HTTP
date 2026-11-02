#pragma once
#include <string>
#include <vector>

namespace mini_http {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;

    std::vector<HttpHeader> headers;
    std::string body{};

};

bool should_keep_alive(const HttpRequest& request);

} // namespace