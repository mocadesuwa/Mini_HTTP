#include "mini_http/static_file/http_handler.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <fstream>
#include <cerrno>
#include <utility>

namespace mini_http {

namespace {

std::string content_type_for(const std::filesystem::path& path) {
    const std::string extent = path.extension().string();

    if(extent == ".html" || extent == ".htm") {
        return "text/html; charset=utf-8";
    }

    if(extent == ".txt") {
        return "text/plain; charset=utf-8";
    }

    if(extent == ".css") {
        return "text/css; charset=utf-8";
    }

    if(extent == ".js") {
        return "text/javascript; charset=utf-8";
    }

    return "application/octet-stream";
}

int hex_value(char ch) {
    if(ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if(ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }

    if(ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }

    return -1;
}

bool decode_request_path(
    std::string_view target, std::string& decoded_path) {
    decoded_path.clear();


    if(target.empty() || target[0] != '/' ||
        target.find('#') != std::string_view::npos) {
        return false;
    }
    const std::string_view encoded_path =
        target.substr(0, target.find('?'));

    for(std::size_t i = 0; i < encoded_path.size(); i++) {
        char decoded = encoded_path[i];

        if(decoded == '%') {
            if(i + 2 >= encoded_path.size()) {
                return false;
            }

            const int high = hex_value(encoded_path[i + 1]);
            const int low = hex_value(encoded_path[i + 2]);
            if(high == -1 || low == -1) {
                return false;
            }

            decoded = static_cast<char>(high * 16 + low);

            i += 2;
        }

        if(decoded == '\0') {
            return false;
        }

        decoded_path.push_back(decoded);
    }

    return true;
}

int read_file_body(
    const std::filesystem::path& file_path,
    std::string& body) {

    body.clear();

    std::error_code ec;
    std::filesystem::file_status file_status =
        std::filesystem::status(file_path, ec);

    if(ec) {
        // 路径中的目录或文件不存在
        // 中间路径组件不是目录

        if(ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory) {
            return 404;
        }

        // 权限不足
        // 操作不被允许
        if(ec == std::errc::permission_denied ||
            ec == std::errc::operation_not_permitted) {
            return 403;
        }

        // 其他查询错误
        return 500;
    }

    if(!std::filesystem::exists(file_status)) {
        return 404;
    }

    if(!std::filesystem::is_regular_file(file_status)) {
        return 403;
    }

    std::ifstream file;
    errno = 0;
    file.open(file_path, std::ios::binary);
    const int open_error = errno;

    if(!file.is_open()) {
        if(open_error == EACCES || open_error == EPERM) {
            return 403;
        }

        if(open_error == ENOENT || open_error == ENOTDIR) {
            return 404;
        }

        return 500;
    }

    std::string contents;
    char buffer[4096];

    while(true) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize count = file.gcount();

        if(count > 0) {
            contents.append(buffer, static_cast<std::size_t>(count));
        }

        if(file.bad()) {
            return 500;
        }

        if(file.eof()) {
            break;
        }

        if(file.fail()) {
            return 500;
        }
    }

    body = std::move(contents);
    return 200;
}

} // inner namespace

HttpResponse handle_request(
    const HttpRequest& request,
    const std::filesystem::path& document_root) {
    HttpResponse response;

    if(request.method != "GET") {
        response.status_code = 405;

        response.body = "Method not allowed\n";

        response.extra_headers.emplace_back("Allow", "GET");

        return response;
    }

    std::filesystem::path file_path;
    int status = resolve_file_path(
        request.target, document_root, file_path);

    if(status == 200) {
        status = read_file_body(file_path, response.body);
    }


    if(status == 400) {
        response.body = "Bad request\n";
    }
    if(status == 403) {
        response.body = "Forbidden\n";
    }
    if(status == 404) {
        response.body = "Not found\n";
    }
    if(status == 500) {
        response.body = "Internal server error\n";
    }

    response.status_code = status;

    if(status == 200) {
        response.content_type = content_type_for(file_path);
    }

    return response;

}

bool is_within_root(
    const std::filesystem::path& root, const std::filesystem::path& candidate) {

    auto root_it = root.begin();
    auto candidate_it = candidate.begin();

    while(root_it != root.end()) {
        if(candidate_it == candidate.end()) {
            return false;
        }

        if(*root_it != *candidate_it) {
            return false;
        }

        ++root_it;
        ++candidate_it;
    }

    return true;
}

int resolve_file_path(
    std::string_view target, const std::filesystem::path &document_root, std::filesystem::path &file_path) {
    file_path.clear();

    std::string decoded_path;
    if(!decode_request_path(target, decoded_path)) {
        return 400;
    }

    if(decoded_path == "/") {
        decoded_path = "/index.html";
    }

    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::canonical(document_root, ec);

    if(ec) {
        return 500;
    }

    if(!std::filesystem::is_directory(root, ec)) {
        return 500;
    }

    const std::filesystem::path relative_path =
        std::filesystem::path(decoded_path).relative_path();

    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root / relative_path, ec);

    if(ec) {
        if(ec == std::errc::permission_denied) {
            return 403;
        }

        if(ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
            return 404;
        }

        return 500;
    }

    if(!is_within_root(root, candidate)) {
        return 403;
    }

    file_path = candidate;
    return 200;

}

} // namespace