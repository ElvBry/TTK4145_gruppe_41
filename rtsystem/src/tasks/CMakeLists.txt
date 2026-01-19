add_library(tasks STATIC
    log_task.c
    stdin_task.c
    dispatcher_task.c
)

target_include_directories(tasks PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(tasks PUBLIC
    core
    Threads::Threads
)
