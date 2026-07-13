add_library(cockpit_ctl_commands STATIC
    dependencies_command.cc
    health_command.cc
    runtime_command.cc
    status_command.cc
)
target_include_directories(cockpit_ctl_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cockpit_ctl_commands
    PRIVATE
        config
        contracts
        diagnostic_cli
        navigator_core
        runtime
        service_health
)

add_executable(cockpit-ctl main.cc)
target_link_libraries(cockpit-ctl PRIVATE
    cockpit_ctl_commands
    config
    diagnostic_cli
    runtime
)
