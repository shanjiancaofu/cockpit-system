add_executable(safe-ota
    main.cc
    action/safe_ota.cc
)
target_include_directories(safe-ota PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(safe-ota PRIVATE
    diagnostic_cli
    navigator_core
    runtime
    upgrader_runtime
)
