#include "mini_http/http_parser.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mini_http::HttpHeader;
using mini_http::HttpParser;
using mini_http::HttpRequest;
using mini_http::ParseResult;

constexpr std::size_t kHeaderLimit = 8 * 1024;
constexpr std::size_t kUnchangedHeaderBytes = 12345;

// 检查在 Debug 和 Release 中都生效；错误信息指出具体用例。
void expect(bool condition, std::string_view case_name, std::string_view message) {
    if(!condition) {
        throw std::runtime_error(
            std::string(case_name) + ": " + std::string(message));
    }
}

HttpRequest make_sentinel_request() {
    return {"SENTINEL", "/unchanged", "HTTP/1.0",
            {{"existing", "keep"}, {"second", "still here"}},
            "old body"};
}

void expect_request_equal(
    const HttpRequest& actual,
    const HttpRequest& expected,
    std::string_view case_name
) {
    expect(actual.method == expected.method, case_name, "method mismatch");
    expect(actual.target == expected.target, case_name, "target mismatch");
    expect(actual.version == expected.version, case_name, "version mismatch");
    expect(actual.headers.size() == expected.headers.size(),
           case_name, "header count mismatch");
    expect(actual.body == expected.body, case_name, "body mismatch");



    for(std::size_t i = 0; i < expected.headers.size(); ++i) {
        expect(actual.headers[i].name == expected.headers[i].name,
               case_name, "header name or order mismatch");
        expect(actual.headers[i].value == expected.headers[i].value,
               case_name, "header value or order mismatch");
    }
}


struct NamedInput {
    const char* name;
    std::string input;
};

void test_request_lines() {
    const HttpParser parser;

    struct ValidCase {
        const char* name;
        const char* input;
        const char* method;
        const char* target;
    };

    const ValidCase valid_cases[] = {
        {"GET root", "GET / HTTP/1.1", "GET", "/"},
        {"GET file", "GET /index.html HTTP/1.1", "GET", "/index.html"},
        {"POST", "POST /submit HTTP/1.1", "POST", "/submit"},
        {"DELETE", "DELETE /item HTTP/1.1", "DELETE", "/item"},
        {"query string", "GET /search?q=cpp HTTP/1.1", "GET", "/search?q=cpp"},
        {"extension method", "X-TEST / HTTP/1.1", "X-TEST", "/"}
    };

    for(const ValidCase& test : valid_cases) {
        HttpRequest request = make_sentinel_request();
        HttpRequest expected = request;
        expected.method = test.method;
        expected.target = test.target;
        expected.version = "HTTP/1.1";

        const bool parsed = parser.parse_request_line(test.input, request);
        expect(parsed, test.name, "valid request line rejected");
        expect_request_equal(request, expected, test.name);
    }

    const std::vector<NamedInput> invalid_cases = {
        {"empty request line", ""},
        {"method only", "GET"},
        {"leading space", " GET /index.html HTTP/1.1"},
        {"missing target", "GET HTTP/1.1"},
        {"empty target", "GET  HTTP/1.1"},
        {"missing version", "GET /"},
        {"invalid method", "G@T /index.html HTTP/1.1"},
        {"tab in method", "G\tET / HTTP/1.1"},
        {"invalid version", "GET / HTTP/x.1"},
        {"unsupported version", "GET / HTTP/1.0"},
        {"version case", "GET / Http/1.1"},
        {"extra field", "GET / HTTP/1.1 extra"},
        {"trailing space", "GET / HTTP/1.1 "},
        {"tab in target", "GET /a\tb HTTP/1.1"},
        {"CRLF in target", "GET /a\r\nb HTTP/1.1"},
        {"DEL in target", std::string("GET /a") + '\x7f' + "b HTTP/1.1"},
        {"NUL in target", std::string("GET /a") + '\0' + "b HTTP/1.1"}
    };

    for(const NamedInput& test : invalid_cases) {
        const HttpRequest before = make_sentinel_request();
        HttpRequest request = before;

        const bool parsed = parser.parse_request_line(test.input, request);
        expect(!parsed, test.name, "invalid request line accepted");
        expect_request_equal(request, before, test.name);
    }
}

void test_header_lines() {
    const HttpParser parser;

    struct ValidCase {
        const char* name;
        const char* input;
        const char* expected_name;
        const char* expected_value;
    };

    const ValidCase valid_cases[] = {
        {"Host address", "Host: 127.0.0.1:8081", "host", "127.0.0.1:8081"},
        {"mixed case and OWS", "HoSt:\tlocalhost \t", "host", "localhost"},
        {"colon in value", "X-Info: a:b", "x-info", "a:b"},
        {"empty value", "X-Empty:", "x-empty", ""},
        {"OWS-only value", "X-Empty: \t ", "x-empty", ""},
        {"internal tab", "X-Info: a\tb", "x-info", "a\tb"},
        {"internal spaces", "X-Info: a  b", "x-info", "a  b"},
        {"value case and slash", "Content-Type: Text/Plain",
         "content-type", "Text/Plain"}
    };

    for(const ValidCase& test : valid_cases) {
        HttpRequest request = make_sentinel_request();
        HttpRequest expected = request;
        expected.headers.push_back({test.expected_name, test.expected_value});

        const bool parsed = parser.parse_header_line(test.input, request);
        expect(parsed, test.name, "valid header line rejected");
        expect_request_equal(request, expected, test.name);
    }

    const std::vector<NamedInput> invalid_cases = {
        {"empty header line", ""},
        {"missing colon", "Host localhost"},
        {"empty name", ": value"},
        {"space before colon", "Host : localhost"},
        {"leading space", " Host: localhost"},
        {"leading tab", "\tHost: localhost"},
        {"invalid name", "X@Name: value"},
        {"CR in value", "X-Test: a\rb"},
        {"LF in value", "X-Test: a\nb"},
        {"CRLF in value", "X-Test: a\r\nb"},
        {"DEL in value", std::string("X-Test: a") + '\x7f' + "b"},
        {"NUL in value", std::string("X-Test: a") + '\0' + "b"}
    };

    for(const NamedInput& test : invalid_cases) {
        const HttpRequest before = make_sentinel_request();
        HttpRequest request = before;

        const bool parsed = parser.parse_header_line(test.input, request);
        expect(!parsed, test.name, "invalid header line accepted");
        expect_request_equal(request, before, test.name);
    }
}

void test_duplicate_headers() {
    const HttpParser parser;
    HttpRequest request = make_sentinel_request();
    HttpRequest expected = request;
    expected.headers.push_back({"x-tag", "a"});
    expected.headers.push_back({"x-tag", "b"});

    const bool first = parser.parse_header_line("X-Tag: a", request);
    const bool second = parser.parse_header_line("x-tag: b", request);

    expect(first && second, "duplicate headers", "header line rejected");
    expect_request_equal(request, expected, "duplicate headers");
}

// 预期长度来自独立构造的 headers，不重复解析器的分隔符查找算法。
void expect_complete_headers(
    std::string_view case_name,
    const std::string& headers,
    std::string_view suffix,
    const HttpRequest& expected
) {
    const HttpParser parser;
    HttpRequest request = make_sentinel_request();
    std::size_t header_bytes = kUnchangedHeaderBytes;
    const std::string input = headers + std::string(suffix);

    const ParseResult result =
        parser.parse_request_headers(input, request, header_bytes);

    expect(result == ParseResult::Complete, case_name, "expected Complete");
    expect(header_bytes == headers.size(), case_name, "wrong header byte count");
    expect(input.substr(header_bytes) == suffix, case_name, "suffix consumed");
    expect_request_equal(request, expected, case_name);
}

void test_complete_headers() {
    const std::string headers =
        "GET /index.html HTTP/1.1\r\n"
        "Host: Example.com\r\n"
        "X-test: Hello\r\n"
        "\r\n";
    const HttpRequest expected = {
        "GET", "/index.html", "HTTP/1.1",
        {{"host", "Example.com"}, {"x-test", "Hello"}}
    };

    expect_complete_headers("complete GET", headers, "", expected);
    expect_complete_headers(
        "following request preserved", headers,
        "GET /next HTTP/1.1\r\nHost: next.example\r\n\r\n", expected);

    expect_complete_headers(
        "POST body preserved",
        "POST /submit HTTP/1.1\r\nHost: localhost:8081\r\n"
        "Content-Length: 4\r\n\r\n",
        "DATA",
        {"POST", "/submit", "HTTP/1.1",
         {{"host", "localhost:8081"}, {"content-length", "4"}}});

    const std::string large_body(9000, 'b');
    expect_complete_headers(
        "large body does not count toward header limit",
        "POST /submit HTTP/1.1\r\nHost: a\r\nContent-Length: 9000\r\n\r\n",
        large_body,
        {"POST", "/submit", "HTTP/1.1",
         {{"host", "a"}, {"content-length", "9000"}}});

    expect_complete_headers(
        "duplicate ordinary fields",
        "GET / HTTP/1.1\r\nhOsT:\ta \t\r\nX-Tag: a\r\nx-tag: b\r\n\r\n",
        "",
        {"GET", "/", "HTTP/1.1",
         {{"host", "a"}, {"x-tag", "a"}, {"x-tag", "b"}}});

    expect_complete_headers(
        "DELETE is syntactically valid",
        "DELETE /item HTTP/1.1\r\nHost: a\r\n\r\n",
        "",
        {"DELETE", "/item", "HTTP/1.1", {{"host", "a"}}});
}

void expect_uncommitted_headers(
    std::string_view case_name,
    std::string_view input,
    ParseResult expected_result
) {
    const HttpParser parser;
    const HttpRequest before = make_sentinel_request();
    HttpRequest request = before;
    std::size_t header_bytes = kUnchangedHeaderBytes;

    const ParseResult result =
        parser.parse_request_headers(input, request, header_bytes);

    expect(result == expected_result, case_name, "unexpected ParseResult");
    expect(header_bytes == kUnchangedHeaderBytes,
           case_name, "header_bytes changed before success");
    expect_request_equal(request, before, case_name);
}

void test_incomplete_and_bad_headers() {
    const NamedInput incomplete_cases[] = {
        {"empty buffer", ""},
        {"partial request line", "GET / HTTP/1."},
        {"request line only", "GET / HTTP/1.1\r\n"},
        {"partial header field", "GET / HTTP/1.1\r\nHost: loca"},
        {"missing empty line", "GET / HTTP/1.1\r\nHost: a\r\n"},
        {"partial final CRLF", "GET / HTTP/1.1\r\nHost: a\r\n\r"}
    };

    for(const NamedInput& test : incomplete_cases) {
        expect_uncommitted_headers(test.name, test.input, ParseResult::NeedMore);
    }

    const std::vector<NamedInput> invalid_cases = {
        {"bad method in request", "G@T / HTTP/1.1\r\nHost: a\r\n\r\n"},
        {"bad target in request", "GET /a\tb HTTP/1.1\r\nHost: a\r\n\r\n"},
        {"bad version in request", "GET / HTTP/1.0\r\nHost: a\r\n\r\n"},
        {"missing Host", "GET / HTTP/1.1\r\nX-Test: a\r\n\r\n"},
        {"no fields", "GET / HTTP/1.1\r\n\r\n"},
        {"empty Host", "GET / HTTP/1.1\r\nHost:\r\n\r\n"},
        {"OWS-only Host", "GET / HTTP/1.1\r\nHost: \t \r\n\r\n"},
        {"duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n"},
        {"same-value duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nhOsT: a\r\n\r\n"},
        {"invalid field after Host", "GET / HTTP/1.1\r\nHost: a\r\nBroken\r\n\r\n"},
        {"space before Host colon", "GET / HTTP/1.1\r\nHost : a\r\n\r\n"},
        {"folded field", "GET / HTTP/1.1\r\nHost: a\r\n X-Test: b\r\n\r\n"},
        {"LF inside field", "GET / HTTP/1.1\r\nHost: a\r\nX-Test: a\nb\r\n\r\n"},
        {"NUL inside field",
         std::string("GET / HTTP/1.1\r\nHost: a\r\nX-Test: a") + '\0' + "b\r\n\r\n"}
    };

    for(const NamedInput& test : invalid_cases) {
        expect_uncommitted_headers(test.name, test.input, ParseResult::BadRequest);
    }
}

void test_header_size_limits() {
    const std::string prefix = "GET / HTTP/1.1\r\nHost: a\r\nX-Pad: ";
    const std::string suffix = "\r\n\r\n";
    const std::size_t framing_bytes = prefix.size() + suffix.size();

    const std::size_t valid_sizes[] = {kHeaderLimit - 1, kHeaderLimit};
    for(std::size_t size : valid_sizes) {
        const std::string padding(size - framing_bytes, 'p');
        const std::string headers = prefix + padding + suffix;
        const HttpRequest expected = {
            "GET", "/", "HTTP/1.1", {{"host", "a"}, {"x-pad", padding}}
        };

        expect(headers.size() == size, "header limit fixture", "wrong fixture size");
        expect_complete_headers(
            size == kHeaderLimit ? "exactly 8192 bytes" : "8191 bytes",
            headers, "", expected);
    }

    const std::string oversized =
        prefix + std::string(kHeaderLimit + 1 - framing_bytes, 'p') + suffix;
    expect_uncommitted_headers(
        "8193 complete header bytes", oversized, ParseResult::BadRequest);

    const std::size_t incomplete_sizes[] = {
        kHeaderLimit - 1, kHeaderLimit, kHeaderLimit + 1
    };
    for(std::size_t size : incomplete_sizes) {
        const std::string incomplete =
            prefix + std::string(size - prefix.size(), 'p');
        const ParseResult expected_result =
            size < kHeaderLimit ? ParseResult::NeedMore : ParseResult::BadRequest;

        expect_uncommitted_headers(
            "header limit without terminator", incomplete, expected_result);
    }
}

void test_fragmented_headers() {
    const HttpParser parser;
    const std::string headers =
        "GET / HTTP/1.1\r\nHost: localhost:8081\r\nX-Test: value\r\n\r\n";
    const HttpRequest expected = {
        "GET", "/", "HTTP/1.1",
        {{"host", "localhost:8081"}, {"x-test", "value"}}
    };
    const HttpRequest before = make_sentinel_request();
    HttpRequest request = before;
    std::size_t header_bytes = kUnchangedHeaderBytes;
    std::string buffer;

    // 每次增加一个字节，覆盖请求行、字段及结束标记的所有拆分位置。
    for(char ch : headers) {
        buffer.push_back(ch);
        const ParseResult result =
            parser.parse_request_headers(buffer, request, header_bytes);

        if(buffer.size() < headers.size()) {
            expect(result == ParseResult::NeedMore,
                   "one-byte fragments", "completed before final byte");
            expect(header_bytes == kUnchangedHeaderBytes,
                   "one-byte fragments", "byte count changed while incomplete");
            expect_request_equal(request, before, "one-byte fragments");
        } else {
            expect(result == ParseResult::Complete,
                   "one-byte fragments", "not complete after final byte");
            expect(header_bytes == headers.size(),
                   "one-byte fragments", "wrong final byte count");
            expect_request_equal(request, expected, "one-byte fragments");
        }
    }
}

void expect_complete_request(
    std::string_view case_name,
    const std::string& headers,
    const std::string& body,
    std::string_view extra,
    const mini_http::HttpRequest& expected
) {
    const HttpParser parser;
    HttpRequest request = make_sentinel_request();
    std::size_t consumed_bytes = kUnchangedHeaderBytes;
    const std::string input = headers + body + std::string(extra);

    const ParseResult result =
        parser.parse_request(input, request, consumed_bytes);

    expect(result == ParseResult::Complete, case_name, "expected Complete");
    expect(consumed_bytes == headers.size() + body.size(), case_name, "wrong consumed byte count");
    expect(input.substr(consumed_bytes) == extra, case_name, "extra data consumed");
    expect_request_equal(request, expected, case_name);
}

void expect_uncommitted_request(
    std::string_view case_name,
    std::string_view input,
    mini_http::ParseResult expected_result
) {
    const HttpParser parser;
    const HttpRequest before = make_sentinel_request();
    HttpRequest request = before;
    std::size_t consumed_bytes = kUnchangedHeaderBytes;

    const ParseResult result =
        parser.parse_request(input, request, consumed_bytes);

    expect(result == expected_result, case_name, "unexpected ParseResult");
    expect(consumed_bytes == kUnchangedHeaderBytes, case_name, "consumed_bytes changed before success");
    expect_request_equal(request, before, case_name);
}

struct RequestCase {
    const char* name;
    std::string input;
    mini_http::ParseResult expected_result;
};

//  通用前缀
const std::string prefix =
        "POST /submit HTTP/1.1\r\n"
        "HOST: a\r\n";

void test_complete_requests() {

    struct BodyCase {
        const char* name;
        const char* length;
        std::string body;
        std::string extra;
    };

    const BodyCase cases[] = {
        {"zero length", "0", "", ""},
        {"normal body", "5", "hello", ""},
        {"extra data preserved", "5", "hello", "NEXT"},
        {"binary body", "3", std::string("a\0b", 3), ""},
        {"whitespace", "4", " a\n ", ""}
    };

    for(const BodyCase& test : cases) {
        const std::string headers =
            prefix + "Content-Length: " + test.length + "\r\n\r\n";


        HttpRequest expected{};
        expected.method = "POST";
        expected.target = "/submit";
        expected.version = "HTTP/1.1";

        expected.headers = {
            {"host", "a"},
            {"content-length", test.length}
        };

        expected.body = test.body;

        expect_complete_request(
            test.name, headers, test.body, test.extra, expected);
    }

    HttpRequest expected{};
    expected.method = "GET";
    expected.target = "/";
    expected.version = "HTTP/1.1";

    expected.headers = {
        {"host", "a"}
    };

    expected.body = "";

    const std::string headers =
        "GET / HTTP/1.1\r\n"
        "Host: a\r\n"
        "\r\n";

    expect_complete_request(
        "GET without Content-Length", headers, "", "", expected);
}

void test_incomplete_and_bad_requests() {
    const RequestCase cases[] = {
        {"empty input", "", ParseResult::NeedMore},
        {"incomplete headers", prefix, ParseResult::NeedMore},
        {"missing body", prefix + "Content-length: 5\r\n\r\n", ParseResult::NeedMore},
        {"partial body", prefix + "Content-Length: 5\r\n\r\nhe", ParseResult::NeedMore},
        {"empty length", prefix + "Content-Length:\r\n\r\n", ParseResult::BadRequest},
        {"negative length", prefix + "Content-Length: -1\r\n\r\n", ParseResult::BadRequest},
        {"positive sign", prefix + "Content-Length: +5\r\n\r\n", ParseResult::BadRequest},
        {"trailing characters", prefix + "Content-Length: 5abc\r\n\r\n", ParseResult::BadRequest},
        {"length overflow", prefix + "Content-Length: " + std::string(100, '9') + "\r\n\r\n", ParseResult::BadRequest},
        {"same duplicate length", prefix + "Content-Length: 5\r\nContent-Length: 5\r\n\r\n", ParseResult::BadRequest},
        {"conflicting lengths", prefix + "Content-Length: 5\r\nContent-Length: 6\r\n\r\n", ParseResult::BadRequest},
        {"unsupported transfer encoding", prefix + "Transfer-Encoding: chunked\r\n\r\n", ParseResult::BadRequest},
        {"length followed by transfer encoding", prefix + "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n", ParseResult::BadRequest}
    };

    for(const RequestCase& test: cases) {
        expect_uncommitted_request(
            test.name, test.input, test.expected_result);
    }
}

void test_body_size_limits() {
    const std::size_t limit = 1024 * 1024;

    struct BodySizeCase {
        const char* name;
        std::size_t declared_length;
        std::size_t actual_length;
        ParseResult expected;
    };

    const BodySizeCase cases[] = {
        {"below limit", limit - 1, limit - 1, ParseResult::Complete},
        {"at limit", limit, limit, ParseResult::Complete},
        {"incomplete", limit, limit - 1, ParseResult::NeedMore},
        {"over limit", limit + 1, 0, ParseResult::BadRequest}
    };

    for(const BodySizeCase& test : cases) {
        const std::string header =
            prefix + "Content-Length: " + std::to_string(test.declared_length) + "\r\n\r\n";

        const std::string body(test.actual_length, 'x');

        if(test.expected == ParseResult::Complete) {
            HttpRequest expected{};

            expected.method = "POST";
            expected.target = "/submit";
            expected.version = "HTTP/1.1";

            expected.headers = {
                {"host", "a"},
                {"content-length", std::to_string(test.declared_length)}
            };

            expected.body = body;

            expect_complete_request(test.name, header, body, "", expected);
        } else {
            expect_uncommitted_request(test.name, header + body, test.expected);
        }
    }

}

void test_fragmented_requests() {
    const std::string headers =
        prefix + "Content-Length: 5\r\n\r\n";
    const std::string body = "hello";
    const std::string input = headers + body;

    for(std::size_t size = 0; size < input.size(); ++size) {
        const std::string case_name = "request prefix " + std::to_string(size);

        expect_uncommitted_request(
                case_name,
                std::string_view(input).substr(0, size),
                ParseResult::NeedMore);
    }

    HttpRequest expected{};

    expected.method = "POST";
    expected.target = "/submit";
    expected.version = "HTTP/1.1";

    expected.headers = {
        {"host", "a"},
        {"content-length", "5"}
    };

    expected.body = body;

    expect_complete_request(
        "without fragment", headers, body, "", expected);

}


} // namespace

int main() {
    struct TestGroup {
        const char* name;
        void (*run)();
    };

    const TestGroup groups[] = {
        {"request lines", test_request_lines},
        {"header lines", test_header_lines},
        {"duplicate headers", test_duplicate_headers},
        {"complete headers", test_complete_headers},
        {"incomplete and bad headers", test_incomplete_and_bad_headers},
        {"header size limits", test_header_size_limits},
        {"fragmented headers", test_fragmented_headers},
        {"complete requests", test_complete_requests},
        {"incomplete and bad headers", test_incomplete_and_bad_requests},
        {"body size limits", test_body_size_limits},
        {"fragmented requests", test_fragmented_requests}
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

    std::cout << "All 11 HTTP parser test groups passed\n";
    return 0;
}