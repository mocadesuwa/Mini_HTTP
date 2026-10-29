#include "mini_http/http_response.h"

#include <stdexcept>
#include <string_view>
namespace mini_http {

namespace  {

std::string_view reason_phrase(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            throw std::invalid_argument("Unsupported HTTP status code");
    }
}

} // inner namespace

std::string HttpResponse::serialize() const {
    std::string result = "HTTP/1.1 ";
    result += std::to_string(status_code);
    result += " ";

    result += reason_phrase(status_code);
    result += "\r\n";

    result += "Content-Type: " + content_type + "\r\n";

    result += "Content-Length: " + std::to_string(body.size()) + "\r\n";

    result += "Connection: close\r\n";

    for(const auto& [name, value] : extra_headers) {
        result += name + ": ";
        result += value + "\r\n";
    }

    result += "\r\n" + body;

    return result;
}

} // namespace