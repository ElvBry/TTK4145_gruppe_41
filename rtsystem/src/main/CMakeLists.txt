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
