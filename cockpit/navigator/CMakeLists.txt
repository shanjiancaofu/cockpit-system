find_package(Threads REQUIRED)

add_library(navigator_core STATIC
    connection/ipc_connector.cc
    diagnostics/crash_reporter.cc
    dl_api/module_loader.cc
    navigator.cc
    process/process_manager.cc
    run_config/run_config.cc
)
target_include_directories(navigator_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(navigator_core PUBLIC json_util logging time_util Threads::Threads
    ${CMAKE_DL_LIBS})

add_executable(cockpit-navigator main.cc)
target_link_libraries(cockpit-navigator PRIVATE navigator_core runtime)
