add_library(util STATIC
    fifo_queue.c
    task_helper.c
)

target_include_directories(util PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(util PUBLIC
    Threads::Threads
)
