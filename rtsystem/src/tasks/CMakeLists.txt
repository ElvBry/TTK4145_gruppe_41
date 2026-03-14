set(TASK_SOURCES
    elevator_task.c process_pair_backup_task.c process_pair_primary_task.c
)

if(ASYNC_LOG)
    list(APPEND TASK_SOURCES log_task.c)
endif()

add_library(tasks STATIC ${TASK_SOURCES})

target_include_directories(tasks PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(tasks PUBLIC
    core
    Threads::Threads
)
