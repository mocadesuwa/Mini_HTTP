#include "mini_http/http_handler.h"

namespace mini_http {

HttpResponse handle_request(const HttpRequest& request) {
    HttpResponse response;

    if(request.method != "GET") {
        response.status_code = 405;

        response.body = "Method not allowed\n";

        response.extra_headers.emplace_back("Allow", "GET");

        return response;
    } else if(request.target == "/") {
        response.body = "Hello, HTTP!\n";

        return response;
    } else {
        response.status_code = 404;

        response.body = "Not found\n";

        return response;
    }

}

} // namespace