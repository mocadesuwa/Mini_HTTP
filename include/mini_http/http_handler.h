#pragma once

#include "mini_http/http_request.h"
#include "mini_http/http_response.h"

namespace mini_http {

HttpResponse handle_request(const HttpRequest& request);


} // namespace