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


project(rtsystem C)

find_package(Threads REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ASYNC_LOG "Use async logging with log_task" ON)
if(ASYNC_LOG)
    message(STATUS "ASYNC_LOG: ON  (disable with -DASYNC_LOG=OFF)")
    add_compile_definitions(ASYNC_LOG)
else()
    message(STATUS "ASYNC_LOG: OFF (enable with -DASYNC_LOG=ON)")
endif()

add_subdirectory(src/core)
add_subdirectory(src/tasks)
add_subdirectory(src/main)


