add_executable(cockpit-ctl
    main.cc
)

target_link_libraries(cockpit-ctl
    PRIVATE
        config
        contracts
        diagnostic_cli
        runtime
        service_health
)
