function(add_mini_http_test name source)
    add_executable(test_${name} ${source})
    target_link_libraries(test_${name} PRIVATE mini_http)
    target_compile_options(test_${name} PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
endfunction()

add_mini_http_test(http_parser http_parser_test.cpp)
add_mini_http_test(http_response http_response_test.cpp)
add_mini_http_test(http_handler http_handler_test.cpp)
add_mini_http_test(http_request http_request_test.cpp)
add_mini_http_test(connection connection_test.cpp)
add_mini_http_test(event_loop event_loop_test.cpp)
add_mini_http_test(path_root path_root_test.cpp)
add_mini_http_test(path_resolve path_resolve_test.cpp)
add_mini_http_test(shutdown_signal shutdown_signal_test.cpp)
add_mini_http_test(logger logger_test.cpp)
add_mini_http_test(config config_test.cpp)

set_tests_properties(event_loop shutdown_signal PROPERTIES TIMEOUT 5)
set_tests_properties(http_handler path_resolve PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_subdirectory(demos)
