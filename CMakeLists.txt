cmake_minimum_required(VERSION 3.20)
project(mini_http
        VERSION 0.1.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(mini_http
            src/server.cpp
            src/http_parser.cpp
            src/http_response.cpp
            src/http_handler.cpp
            src/http_request.cpp
            src/event_loop.cpp
            src/acceptor.cpp
            src/connection.cpp
            src/thread_pool.cpp)

target_link_libraries(mini_http PUBLIC Threads::Threads)

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

add_executable(test_http_request tests/http_request_test.cpp)
target_link_libraries(test_http_request PRIVATE mini_http)

add_executable(test_path_root tests/path_root_test.cpp)
target_link_libraries(test_path_root PRIVATE mini_http)

add_executable(test_path_resolve tests/path_resolve_test.cpp)
target_link_libraries(test_path_resolve PRIVATE mini_http)

enable_testing()
add_test(NAME http_parser COMMAND test_http_parser)
add_test(NAME http_response COMMAND test_http_response)
add_test(NAME http_handler COMMAND test_http_handler)
add_test(NAME http_request COMMAND test_http_request)
add_test(NAME path_root COMMAND test_path_root)
add_test(NAME path_resolve COMMAND test_path_resolve)
set_tests_properties(http_handler path_resolve PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)


target_compile_options(test_http_parser
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(mini_http
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(mini_http_server
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(test_http_response
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(test_http_handler
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(test_http_request
    PRIVATE -Wall -Wextra -Wpedantic)


target_compile_options(test_path_root
    PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(test_path_resolve
    PRIVATE -Wall -Wextra -Wpedantic)

# 手动观察示例：按需运行，不注册到 CTest。

add_executable(thread_pool_demo tests/demos/thread_pool.cpp)
target_link_libraries(thread_pool_demo PRIVATE mini_http)

add_executable(eventfd_demo tests/demos/eventfd.cpp)
target_link_libraries(eventfd_demo PRIVATE mini_http)

add_executable(post_demo tests/demos/post.cpp)
target_link_libraries(post_demo PRIVATE mini_http)
