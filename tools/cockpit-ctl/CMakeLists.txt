add_executable(cockpit-ctl
    main.cc
    health_command.cc
    runtime_command.cc
    snapshot_command.cc
    status_command.cc
)
target_include_directories(cockpit-ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cockpit-ctl
    PRIVATE
        config
        build_info
        contracts
        diagnostic_cli
        navigator_core
        runtime
        service_health
)
