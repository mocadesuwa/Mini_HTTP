cmake_minimum_required(VERSION 3.20)
project(mini_http
        VERSION 0.1.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(mini_http
            src/server.cpp
            src/http_parser.cpp
            src/http_response.cpp
            src/http_handler.cpp)

target_include_directories(mini_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(mini_http_server main.cpp)
target_link_libraries(mini_http_server PRIVATE mini_http)

add_executable(test_http_parser tests/http_parser_test.cpp)
target_link_libraries(test_http_parser PRIVATE mini_http)

add_executable(test_http_response tests/http_response_test.cpp)
target_link_libraries(test_http_response PRIVATE mini_http)

add_executable(test_http_handler tests/http_handler_test.cpp)
target_link_libraries(test_http_handler PRIVATE mini_http)


enable_testing()
add_test(NAME http_parser COMMAND test_http_parser)
add_test(NAME http_response COMMAND test_http_response)
add_test(NAME http_handler COMMAND test_http_handler)


target_compile_options(test_http_parser
    PRIVATE -Wall -Wextra -Wpedantic
)

target_compile_options(mini_http
    PRIVATE -Wall -Wextra -Wpedantic
)

target_compile_options(mini_http_server
    PRIVATE -Wall -Wextra -Wpedantic
)

target_compile_options(test_http_response
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(test_http_handler
    PRIVATE -Wall -Wextra -Wpedantic)