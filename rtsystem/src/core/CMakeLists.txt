add_library(core STATIC
    fifo_queue.c
)

target_include_directories(core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC
    Threads::Threads
)
