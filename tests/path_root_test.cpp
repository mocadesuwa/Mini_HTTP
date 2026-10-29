#include "mini_http/http_handler.h"

#include <iostream>

int main() {
    struct TestCase {
        const char* candidate;
        bool expected;
    };

    const std::filesystem::path root{"/srv/www"};
    const TestCase cases[] = {
        {"/srv/www/index.html", true},
        {"/srv/www/static/hello.html", true},
        {"/srv/www2/test.txt", false},
        {"/srv/secret.txt", false},
        {"/srv", false},
        {"/srv/www", true}
    };

    bool all_passed = true;

    for (const TestCase& test_case : cases) {
        const bool actual = mini_http::is_within_root(
            root, std::filesystem::path{test_case.candidate});

        const bool passed = actual == test_case.expected;

        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << test_case.candidate
                  << " expected=" << std::boolalpha << test_case.expected
                  << " actual=" << actual << '\n';

        if (!passed) {
            all_passed = false;
        }
    }

    return all_passed ? 0 : 1;
}