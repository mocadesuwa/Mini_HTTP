#include "mini_http/http_parser.h"
#include <utility>
#include <limits>
namespace mini_http {

namespace  {

bool is_tchar(char ch) {
    if ((ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9')) {
        return true;
    }

    switch(ch) {
        case '!': return true;
        case '#': return true;
        case '$': return true;
        case '&': return true;
        case '%': return true;
        case '*': return true;
        case '+': return true;
        case '-': return true;
        case '.': return true;
        case '^': return true;
        case '_': return true;
        case '|': return true;
        case '~': return true;
        case '`': return true;
        case '\'': return true;
        default: return false;
    }
}

bool is_valid_http_method(std::string_view method) {
    if(method.empty()) {
        return false;
    }

    for(char ch : method) {
        if(!is_tchar(ch)) {
            return false;
        }
    }
    return true;
}

bool is_valid_http_target(std::string_view target) {
    for(char ch : target) {
        const unsigned char byte = static_cast<unsigned char>(ch);

        if(byte <= 0x20 || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

bool is_valid_http_version(std::string_view version) {
    return version == "HTTP/1.1";
}

bool is_valid_header_name(std::string_view name) {
    for(char ch : name) {
        if(!is_tchar(ch)) {
            return false;
        }
    }
    return true;
}

bool is_valid_header_value(std::string_view value) {
    for(char ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);

        if((byte < 0x20 && byte != '\t') || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

void trim_ows(std::string_view& value) {
    while(!value.empty() &&
            (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
    }

    while(!value.empty() &&
            (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
    }
}

}

bool HttpParser::parse_request_line(
            std::string_view line,
            HttpRequest& request
        ) const {

        const std::size_t first_space = line.find(' ');

        if(first_space == std::string_view::npos || first_space == 0) {
            return false;
        }

        const std::size_t second_space = line.find(' ', first_space + 1);

        if(second_space == std::string_view::npos || second_space == first_space + 1) {
            return false;
        }

        const std::string_view method = line.substr(0, first_space);
        const std::string_view target = line.substr(first_space + 1, second_space - first_space -1);
        const std::string_view version = line.substr(second_space + 1);

        if(!is_valid_http_method(method)) {
            return false;
        }

        if(!is_valid_http_target(target)) {
            return false;
        }

        if(!is_valid_http_version(version)) {
            return false;
        }

        request.method = method;
        request.target = target;
        request.version = version;

        return true;
}

bool HttpParser::parse_header_line(
                std::string_view line,
                HttpRequest& request
            ) const {

        const std::size_t first_colon = line.find(':');

        if(first_colon == std::string_view::npos || first_colon == 0) {
            return false;
        }

        std::string_view name_view = line.substr(0, first_colon);
        std::string_view value = line.substr(first_colon + 1);

        if(!is_valid_header_name(name_view)) {
            return false;
        }

        std::string name(name_view);

        for(char& ch : name) {
            if(ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch + 'a' - 'A');
            }
        }

        trim_ows(value);

        if(!is_valid_header_value(value)) {
            return false;
        }

        request.headers.push_back(
            HttpHeader{std::move(name), std::string(value)});

        return true;
}

ParseResult HttpParser::parse_request_headers(
    std::string_view buffer,
    HttpRequest& request,
    std::size_t& header_bytes
) const {
    constexpr std::size_t MAX_HEADED_SIZE = 8 * 1024;

    const std::size_t header_end = buffer.find("\r\n\r\n");

    if(header_end == std::string_view::npos) {
        return buffer.size() >= MAX_HEADED_SIZE
        ? ParseResult::BadRequest
        : ParseResult::NeedMore;
    }

    const std::size_t total_header_bytes = header_end + 4;

    if(total_header_bytes > MAX_HEADED_SIZE) {
        return ParseResult::BadRequest;
    }

    const std::size_t line_end = buffer.find("\r\n");
    if(line_end == std::string_view::npos ||
        line_end > header_end) {
            return ParseResult::BadRequest;
        }

    std::string_view request_line = buffer.substr(0, line_end);

    HttpRequest parsed{};
    if(!parse_request_line(request_line, parsed)) {
        return ParseResult::BadRequest;
    }

    std::size_t cursor = line_end + 2;

    while(cursor < header_end) {
        const std::size_t next = buffer.find("\r\n", cursor);

        if(next == std::string_view::npos || next > header_end) {
            return ParseResult::BadRequest;
        }

        std::string_view header_line =
            buffer.substr(cursor, next - cursor);

        if(!parse_header_line(header_line, parsed)) {
            return ParseResult::BadRequest;
        }

        cursor = next + 2;

    }

    std::size_t host_count = 0;

    for(const HttpHeader& header : parsed.headers) {
        if(header.name == "host") {
            ++host_count;

            if(header.value.empty()) {
                return ParseResult::BadRequest;
            }
        }
    }

    if(host_count != 1) {
        return ParseResult::BadRequest;
    }


    request = std::move(parsed);
    header_bytes = total_header_bytes;

    return ParseResult::Complete;
}


ParseResult HttpParser::parse_request(
    std::string_view buffer,
    HttpRequest& request,
    std::size_t& consumed_bytes
) const {
    constexpr std::size_t MAX_BODY_SIZE = 1024 * 1024;

    HttpRequest parsed{};
    std::size_t header_bytes = 0;

    const ParseResult result =
        parse_request_headers(buffer, parsed, header_bytes);

    if(result != ParseResult::Complete) {
        return result;
    }

    std::size_t content_length = 0;
    bool has_content_length = false;

    for(const HttpHeader& header : parsed.headers) {
        if(header.name == "content-length") {
            if(has_content_length) {
                return ParseResult::BadRequest;
            }

            if(header.value.empty()) {
                return ParseResult::BadRequest;
            }

            std::size_t sum = 0;
            for(char c : header.value) {
                if(c < '0' || c > '9') {
                    return ParseResult::BadRequest;
                }

                const std::size_t digit =
                            static_cast<std::size_t>(c - '0');

                if(sum > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
                    return ParseResult::BadRequest;
                }
                sum = sum * 10 + digit;

                content_length = sum;
                has_content_length = true;
            }
        }

        if(header.name == "transfer-encoding") {
            return ParseResult::BadRequest;
        }
    }


    if(content_length > MAX_BODY_SIZE) {
        return ParseResult::BadRequest;
    }

    if(buffer.size() - header_bytes < content_length) {
        return ParseResult::NeedMore;
    }

    parsed.body = std::string(
        buffer.substr(header_bytes, content_length)
    );

    const std::size_t total_bytes =
        header_bytes + content_length;

    request = std::move(parsed);
    consumed_bytes = total_bytes;

    return ParseResult::Complete;
}

}