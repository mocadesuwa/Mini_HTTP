#include "mini_http/static_file/http_handler.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <fstream>

namespace {

void expect(bool condition, std::string_view case_name, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

void test_valid_paths() {

    const std::filesystem::path document_root{
        "build/path_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    const std::filesystem::path expected_root =
        std::filesystem::canonical(document_root, ec);

    expect(!ec, "prepare document root", "failed to canonicalize document root");

    struct ValidPathCase {
        std::string_view name;
        std::string_view target;
        std::filesystem::path expected_path;
    };


    const std::filesystem::path expected_file =
        expected_root / "index.html";

    const std::filesystem::path expected_nested_file =
        expected_root / "static" / "hello.html";

    const ValidPathCase cases[] = {
        {"root maps to index", "/", expected_file},
        {"nested path", "/static/hello.html", expected_nested_file},
        {"path with query", "/static/hello.html?v=1", expected_nested_file},
        {"percent encoded path", "/static/%68ello.html", expected_nested_file}
    };


    for(const ValidPathCase& test : cases) {
        std::filesystem::path actual_file;

        const int status = mini_http::resolve_file_path(
            test.target, document_root, actual_file);

        expect(status == 200, test.name, "expected status 200");
        expect(actual_file == test.expected_path, test.name, "resolved path mismatch");
    }
}

void test_invalid_targets() {
    struct InvalidTargetCase {
        std::string_view name;
        std::string_view target;
    };

    std::filesystem::path document_root{
        "build/path_test_data/www"
    };

    std::filesystem::path actual_file{
        "sentinel"
    };

    const InvalidTargetCase cases[] = {
        {"incomplete percent escape", "/static/%6"},
        {"invalid hex digit", "/static/%6G"},
        {"encoded NUL", "/static/hello%00.html"},
        {"empty target", ""},
        {"missing leading slash", "static/hello.html"},
        {"raw fragment", "/index.html#part"}
    };

    for(const InvalidTargetCase& test : cases) {
        actual_file = "senitel";

        const int status = mini_http::resolve_file_path(
            test.target, document_root, actual_file);

        expect(status == 400, test.name, "expected status 400");
        expect(actual_file.empty(), test.name, "expected output path to be empty");
    }

}

void test_root_escape() {
    const std::filesystem::path document_root {
        "build/path_test_data/www"
    };

    const std::filesystem::path outside_dir {
        "build/path_test_data/outside"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    std::filesystem::create_directories(outside_dir, ec);
    expect(!ec, "prepare outside directory", "failed to create outside directory");

    const std::filesystem::path outside_root =
        std::filesystem::canonical(outside_dir, ec);

    expect(!ec, "prepare outside directory", "failed to canonicalize outside directory");

    const std::filesystem::path link_path =
        document_root / "escape";

    std::filesystem::remove(link_path, ec);

    expect(!ec, "prepare escape symlink", "failed to remove previous escape symlink");

    std::filesystem::create_directory_symlink(outside_root, link_path, ec);

    expect(!ec, "prepare escape symlink", "failed to create escape symlink");
    struct RootEscapeCase {
        std::string_view name;
        std::string_view target;
    };

    const RootEscapeCase cases[] = {
        {"plain traversal", "/../outside/secret.txt"},
        {"encoded traversal", "/%2e%2e/outside/secret.txt"},
        {"symlink escape", "/escape/secret.txt"}
    };

    for(const RootEscapeCase& test : cases) {
        std::filesystem::path actual_file{
            "sentinel"
        };

        const int status = mini_http::resolve_file_path(
            test.target, document_root, actual_file);

        expect(status == 403, test.name, "expected status 403");
        expect(actual_file.empty(), test.name, "expected output path to be empty");
    }

}

void test_invalid_document_root() {
    const std::filesystem::path base_dir {
        "build/path_test_data"
    };

    const std::filesystem::path missing_root =
        base_dir / "missing_root";

    const std::filesystem::path file_root =
        base_dir / "file_root.txt";

    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);

    expect(!ec, "prepare test directory", "failed to create test directory");

    std::filesystem::remove(missing_root, ec);

    expect(!ec, "prepare missing root", "failed to remove document root fixture");

    std::ofstream file_stream{file_root};

    expect(file_stream.is_open(), "prepare file root", "failed to create root fixture file");

    file_stream.close();

    struct InvalidRootCase {
        std::string_view name;
        std::filesystem::path document_root;
    };

    const InvalidRootCase cases[] = {
        {"missing document root", missing_root},
        {"file as document root", file_root}
    };

    for(const InvalidRootCase& test : cases) {
        std::filesystem::path actual_file {
            "sentinel"
        };

        const int status = mini_http::resolve_file_path(
            "/", test.document_root, actual_file);

        expect(status == 500, test.name, "expected status 500");
        expect(actual_file.empty(), test.name, "expected output path to be empty");
    }
}

void test_missing_target_path() {
    const std::filesystem::path document_root {
        "build/path_test_data/www"
    };

    std::error_code ec;
    std::filesystem::create_directories(document_root, ec);

    expect(!ec, "prepare document root", "failed to create document root");

    const std::filesystem::path missing_file =
        document_root / "not-found.txt";

    std::filesystem::remove(missing_file, ec);

    expect(!ec, "prepare missing target", "failed to remove target file fixture");

    const std::filesystem::path expected_root =
        std::filesystem::canonical(document_root, ec);

    expect(!ec, "prepare document root", "failed to canonicalize document root");

    struct MissingTargetCase {
        std::string_view name;
        std::string_view target;
        std::filesystem::path expected_path;
    };

    const MissingTargetCase cases[] = {
        {"missing target file",
             "/not-found.txt",
             expected_root / "not-found.txt"}
    };

    for(const MissingTargetCase& test : cases) {
        std::filesystem::path actual_file {
            "sentinel"
        };

        const int status = mini_http::resolve_file_path(
            test.target, document_root, actual_file);

        expect(status == 200, test.name, "expected status 200");
        expect(actual_file == test.expected_path, test.name, "resolved path mismatch");
    }
}

} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"valid paths", test_valid_paths},
        {"invalid targets", test_invalid_targets},
        {"root escape", test_root_escape},
        {"invalid document root", test_invalid_document_root},
        {"missing target path", test_missing_target_path}
    };

    for (const TestGroup& group : groups) {
        try {
            group.run();
            std::cout << "[PASS] " << group.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << group.name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "All 5 path resolver test groups pass\n";

    return 0;
}
