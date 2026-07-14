add_executable(cockpit-ctl
    main.cc
    dependencies_command.cc
    health_command.cc
    runtime_command.cc
    status_command.cc
)
target_include_directories(cockpit-ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cockpit-ctl
    PRIVATE
        config
        contracts
        diagnostic_cli
        navigator_core
        runtime
        service_health
)
