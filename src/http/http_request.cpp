#include "mini_http/http/http_request.h"

#include <string>
#include <string_view>

namespace mini_http {

bool should_keep_alive(const HttpRequest& request) {

    for(const HttpHeader& header : request.headers) {
        if(header.name != "connection") {
            continue;
        }

        std::string_view remaing = header.value;

        while(true) {
            const std::size_t comma = remaing.find(',');
            std::string_view token = remaing.substr(0, comma);

            while(!token.empty() &&
                (token.front() == ' ' || token.front() == '\t')) {
                token.remove_prefix(1);
            }

            while(!token.empty() &&
                (token.back() == ' ' || token.back() == '\t')) {
                token.remove_suffix(1);
            }


            std::string normalized(token);
            for(char& ch : normalized) {
                if(ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            if(normalized == "close") {
                return false;
            }


            if(comma == std::string_view::npos) {
                break;
            }

            remaing.remove_prefix(comma + 1);
        }
    }
    return true;
}


} // namespace