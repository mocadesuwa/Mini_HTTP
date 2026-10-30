#pragma once

#include <string>
#include <utility>
#include <vector>
namespace mini_http {

struct HttpResponse {
    int status_code{200};
    std::string content_type{"text/plain; charset=utf-8"};
    std::string body{};

    std::vector<std::pair<std::string, std::string>> extra_headers;

    bool keep_alive{false};

    std::string serialize() const;
};

} // namespace