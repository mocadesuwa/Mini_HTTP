#pragma once
#include <string_view>
#include "mini_http/http/http_request.h"

namespace mini_http {

enum class ParseResult {
    Complete,
    NeedMore,
    BadRequest
};

class HttpParser {
public:
    bool parse_request_line(
        std::string_view line,
        HttpRequest& request
    ) const;

    bool parse_header_line(
        std::string_view line,
        HttpRequest& request
    ) const;

    ParseResult parse_request_headers(
        std::string_view buffer,
        HttpRequest& request,
        std::size_t& header_bytes
    ) const;

    ParseResult parse_request(
        std::string_view buffer,
        HttpRequest& request,
        std::size_t& consumed_bytes
    ) const;
};

} // namespace