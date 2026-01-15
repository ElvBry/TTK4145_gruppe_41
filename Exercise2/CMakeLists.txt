cmake_minimum_required(VERSION 3.13)
project(Exercise2 C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(udp_receiver)
add_subdirectory(udp_sender)
