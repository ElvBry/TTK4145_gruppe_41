add_library(tasks STATIC
    log_task.c
)

target_include_directories(tasks PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(tasks PUBLIC
    Threads::Threads
    rt
)
