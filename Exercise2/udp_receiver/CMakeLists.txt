add_executable(udp_receiver src/udp_receiver.c)
target_link_libraries(udp_receiver PRIVATE log_helper)
target_compile_options(udp_receiver PRIVATE -Wall -Wextra)