#include "mini_http/static_file/http_handler.h"

#include <stdexcept>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>
#include <string_view>
#include <iostream>

namespace {

using mini_http::HttpRequest;
using mini_http::HttpResponse;
using mini_http::handle_request;

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_get_root() {
    // TODO: 创建独立的 index.html，传入根目录，检查正文和 HTML 类型。
    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    std::string index_body = "<h1>Home</h1>\n";

    std::ofstream index_stream{
        document_root / "index.html",
        std::ios::binary
    };

    expect(
        index_stream.is_open(),
        "prepare index file",
        "failed to open index file");

    index_stream << index_body;
    index_stream.close();

    expect(
        !index_stream.fail(),
        "prepare index file",
        "failed to write or close index file");

    HttpRequest request = {
        "GET", "/", "HTTP/1.1", {}, ""
    };


    const HttpResponse response = handle_request(request, document_root);

    expect(response.status_code == 200,
            "get root", "expected status 200");
    expect(response.body == index_body,
            "get root", "root response body dismatch");
    expect(response.content_type == "text/html; charset=utf-8",
            "get root", "content-type dismatch");
}

void test_get_not_found() {
    // TODO: 准备存在的根目录及不存在的目标，将根目录传给新接口。
    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root ", "failed to create root");

    std::filesystem::remove(document_root / "missing", ec);

    expect(!ec, "remove missing", "failed to remove missing");

    HttpRequest request = {
        "GET", "/missing", "HTTP/1.1",
        {}, ""
    };

    const HttpResponse response = handle_request(request, document_root);

    expect(response.status_code == 404,
        "get not found", "expected status 404");
    expect(response.body == "Not found\n",
        "get not found", "not-found response body mismatch");
}


void test_method_not_allowed() {
    // TODO: 为新接口传入根目录，保留已有 405 正文和 Allow 断言。
    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    const HttpRequest cases[] = {
        {"POST", "/", "HTTP/1.1",
            {}, ""},
        {"DELETE", "/", "HTTP/1.1",
            {}, ""},
            {"POST", "/missinh", "HTTP/1.1",
            {}, ""}
    };

    for(const HttpRequest& request : cases) {
        const std::string case_name = request.method + " " + request.target;

        const HttpResponse response = handle_request(request, document_root);

        expect(response.status_code == 405,
            case_name, "expected status 405");

        expect(response.body == "Method not allowed\n",
            case_name, "wrong body");

        expect(response.extra_headers.size() == 1,
            case_name, "expected exactly one extra header");

        const auto& header = response.extra_headers.front();

        expect(header.first == "Allow",
            case_name, "expected Allow header");

        expect(header.second == "GET",
            case_name, "Allow value must be GET");
    }
}

void test_static_files() {
    // TODO: 使用独立测试目录和结构体数组，检查真实文件正文与 Content-Type。
    struct StaticFilesCase {
        std::string_view name;
        std::filesystem::path relative_path;
        std::string_view body;
        std::string_view expected_content_type;
    };

    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    const StaticFilesCase cases[] = {
        {"text file", "test.txt",
             "Hello, text!\n","text/plain; charset=utf-8"},
        {"html file", "static/hello.html",
             "Hello html!\n", "text/html; charset=utf-8"},
        {"htm file", "page.htm",
             "Hello htm!\n", "text/html; charset=utf-8"},
        {"css file", "style.css",
             "Hello css!\n", "text/css; charset=utf-8"},
        {"js file", "app.js",
             "Hello javascript!\n", "text/javascript; charset=utf-8"},
        {"bin file", "data.bin",
             "Hello binary!\n", "application/octet-stream"}
    };

    for(const StaticFilesCase& test : cases) {
        const std::filesystem::path file_path =
            document_root / test.relative_path;

        std::filesystem::create_directories(file_path.parent_path(), ec);

        expect(!ec, "prepare file's parent directory", "failed to create directory");

        std::ofstream file_stream {
            file_path,
            std::ios::binary
        };

        expect(file_stream.is_open(), test.name, "failed to open file");

        file_stream << test.body;
        file_stream.close();

        expect(!file_stream.fail(), test.name, "failed to write or close file");

        const std::string target =
            "/" + test.relative_path.generic_string();

        HttpRequest request = {
            "GET", target, "HTTP/1.1", {}, ""};

        HttpResponse response = handle_request(request, document_root);

        expect(response.status_code == 200,
                test.name, "expected status 200");
        expect(response.body == test.body,
                test.name, "response body dismatch");
        expect(response.content_type == test.expected_content_type,
                test.name, "content-type dismatch");
    }
}

void test_binary_and_empty_files() {
    // TODO: 覆盖空文件和含 NUL 的文件，检查正文字节及序列化的 Content-Length。
    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    std::string binary_body(5000, 'x');
    binary_body[123] = '\0';
    binary_body += "\r\n";

    struct BinaryFileCase {
        std::string_view name;
        std::filesystem::path relative_path;
        std::string body;
    };

    const BinaryFileCase cases[] = {
        {"empty file", "empty.bin", ""},
        {"binary file", "binary.bin", binary_body}
    };

    for(const BinaryFileCase& test : cases) {
        std::ofstream stream {
            document_root / test.relative_path,
            std::ios::binary
        };

        expect(stream.is_open(), test.name, "failed to open file");

        stream.write(test.body.data(), static_cast<std::streamsize>(test.body.size()));

        stream.close();

        expect(!stream.fail(), test.name, "failed to write or close file");
        const std::string target =
            "/" + test.relative_path.generic_string();

        HttpRequest request = {
            "GET", target, "HTTP/1.1", {}, ""};

        HttpResponse response = handle_request(request, document_root);

        expect(response.status_code == 200,
                test.name, "expected status 200");
        expect(response.body == test.body,
                test.name, "response body dismatch");
        expect(response.content_type == "application/octet-stream",
                test.name, "content-type dismatch");

        const std::string serialized = response.serialize();
        const std::size_t separator = serialized.find("\r\n\r\n");

        expect(separator != std::string::npos,
                test.name, "cannot find searator");

        const std::size_t body_start = separator + 4;
        const std::string headers = serialized.substr(0, body_start);
        const std::string actual_body = serialized.substr(body_start);

        const std::string expected_header =
            "\r\nContent-Length: " +
            std::to_string(test.body.size()) + "\r\n";

        expect(headers.find(expected_header) != std::string::npos,
                test.name, "cannot find correct header");
        expect(actual_body == test.body,
                test.name, "incomplete body");
    }

}

void test_file_errors() {
    // TODO: 覆盖非法目标、目录、越界、无效根目录和可实际触发的读取失败。

    const std::filesystem::path document_root {
        "build/handler_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root ", "failed to create root");

    const std::filesystem::path missing_root {
        document_root / "missing_root"
    };
    std::filesystem::remove(document_root / "missing_root", ec);

    expect(!ec, "remove missing", "failed to remove missing");

    std::filesystem::create_directories(document_root / "static", ec);
    expect(!ec,"prepare static directory",
            "failed to create static directory");

    struct FileErrorsCase {
        std::string_view name;
        std::filesystem::path root_directory;
        std::string target;
        int expected_status;
        std::string_view body;
    };

    const FileErrorsCase cases[] = {
    {"invalid encoding", document_root, "/static/%6",
     400, "Bad request\n"},
    {"directory target", document_root, "/static/",
     403, "Forbidden\n"},
    {"root escape", document_root, "/../outside/secret.txt",
     403, "Forbidden\n"},
    {"missing root", missing_root, "/",
     500, "Internal server error\n"}
    };

    for(const FileErrorsCase& test : cases) {
        HttpRequest request{
            "GET", test.target, "HTTP/1.1", {}, ""};

        const HttpResponse response =
            handle_request(request, test.root_directory);

        expect(response.status_code == test.expected_status,
                test.name, "expected status");
        expect(response.body == test.body,
                test.name, "body dismatch");
        expect(response.content_type == "text/plain; charset=utf-8",
                test.name, "content-type dismatch");
    }
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"get root", test_get_root},
        {"get not found", test_get_not_found},
        {"method not allowed", test_method_not_allowed},
        {"static files", test_static_files},
        {"binary and empty files", test_binary_and_empty_files},
        {"file errors", test_file_errors}
    };

    for(const TestGroup& group : groups) {
        try {
            group.run();
            std::cout << "[PASS] " << group.name << '\n';
        } catch(const std::exception& error) {
            std::cerr << "[FAIL] " << group.name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "All 6 HTTP handler test groups pass\n";

    return 0;
}