cmake_minimum_required(VERSION 3.13)

  # Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
else()
    message(STATUS "ccache not found, building without it")
endif()


project(Exercise2 C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(udp_receiver)
add_subdirectory(tcp_receiver)
add_subdirectory(udp_tcp_sender)
