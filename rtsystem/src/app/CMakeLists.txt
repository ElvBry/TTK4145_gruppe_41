add_library(app STATIC
    elevator_fsm.c
    elevator_roles.c
    elevator_control.c
)

target_include_directories(app PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(app PUBLIC
    drivers
    util
)
