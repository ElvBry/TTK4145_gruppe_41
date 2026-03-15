add_executable(rtsystem main.c)

target_include_directories(rtsystem PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(rtsystem PRIVATE
    -Wall -Wextra
    -Werror=implicit-function-declaration
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

target_link_libraries(rtsystem PRIVATE
    tasks
    core
)

# Grant the binary permission to use SCHED_FIFO without running as root.
# Run once after each build: make -C build setcap
add_custom_target(setcap
    COMMAND sudo setcap cap_sys_nice+ep $<TARGET_FILE:rtsystem>
    DEPENDS rtsystem
    COMMENT "Granting cap_sys_nice to rtsystem (allows SCHED_FIFO without sudo)"
)

# Send SIGINT to all running rtsystem processes for a graceful shutdown.
# Useful after kill -9 on the primary leaves a promoted backup running.
# Works from the same terminal: make -C build kill
add_custom_target(kill
    COMMAND pkill -2 -x rtsystem || true
    COMMENT "Sending SIGINT to all rtsystem processes"
)
