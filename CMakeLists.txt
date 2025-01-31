project(tcp_example)
add_compile_options(-Wall -Wextra -O2 -s)

add_executable(tcp_server tcp_server.c)
add_executable(tcp_client tcp_client.c)