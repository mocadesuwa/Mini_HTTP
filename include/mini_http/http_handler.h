#pragma once

#include "mini_http/http_request.h"
#include "mini_http/http_response.h"

#include <filesystem>
#include <string_view>

namespace mini_http {

HttpResponse handle_request(
    const HttpRequest& request,
    const std::filesystem::path& document_root);

bool is_within_root(
    const std::filesystem::path& root, const std::filesystem::path& candidate);

int resolve_file_path(
    std::string_view target,
    const std::filesystem::path& document_root,
    std::filesystem::path& file_path);

} // namespace