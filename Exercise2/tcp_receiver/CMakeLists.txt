add_executable(tcp_receiver src/tcp_receiver.c)
target_link_libraries(tcp_receiver PRIVATE log_helper)
target_compile_options(tcp_receiver PRIVATE -Wall -Wextra)
