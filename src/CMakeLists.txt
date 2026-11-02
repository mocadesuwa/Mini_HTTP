add_library(mini_http
    server/server.cpp
    server/config.cpp
    server/shutdown_signal.cpp
    reactor/event_loop.cpp
    reactor/acceptor.cpp
    reactor/connection.cpp
    http/http_request.cpp
    http/http_parser.cpp
    http/http_response.cpp
    static_file/http_handler.cpp
    runtime/thread_pool.cpp
    runtime/logger.cpp
)

target_include_directories(mini_http
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(mini_http PUBLIC Threads::Threads)
target_compile_options(mini_http PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mini_http_server main.cpp)
target_link_libraries(mini_http_server PRIVATE mini_http)
