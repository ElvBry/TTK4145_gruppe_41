add_executable(rtsystem main.c)

target_include_directories(rtsystem PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rtsystem PRIVATE
    tasks
)
