add_executable(udp_tcp_sender src/udp_tcp_sender.c)
target_link_libraries(udp_tcp_sender PRIVATE log_helper)
target_compile_options(udp_tcp_sender PRIVATE -Wall -Wextra)
