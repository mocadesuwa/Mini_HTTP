# 手动观察示例：只构建，不注册到CTest。
add_executable(thread_pool_demo thread_pool.cpp)
target_link_libraries(thread_pool_demo PRIVATE mini_http)

add_executable(eventfd_demo eventfd.cpp)
target_link_libraries(eventfd_demo PRIVATE mini_http)

add_executable(post_demo post.cpp)
target_link_libraries(post_demo PRIVATE mini_http)
