add_executable(udp_sender src/udp_sender.c)
target_link_libraries(udp_sender PRIVATE log_helper)
target_compile_options(udp_sender PRIVATE -Wall -Wextra)
