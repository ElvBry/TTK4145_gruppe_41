add_library(drivers STATIC
    elevator_hardware.c
    elevator_network.c
)

target_include_directories(drivers PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(drivers PUBLIC
    Threads::Threads
)
