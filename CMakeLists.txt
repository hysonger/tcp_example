cmake_minimum_required(VERSION 3.20)

project(tcp_example)
# 指定C++17和C11
add_compile_options(-Wall -Wextra -O2 -s --std=c++17 --std=c11)

file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.h)
add_executable(tcp_server tcp_server_v2.cpp ${SRC_FILES})
add_executable(tcp_client tcp_client_v2.cpp ${SRC_FILES})

target_include_directories(tcp_server PRIVATE include)
target_include_directories(tcp_client PRIVATE include)