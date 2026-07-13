find_package(Threads REQUIRED)

add_library(navigator_core STATIC
    connection/ipc_connector.cc
    dl_api/module_loader.cc
    navigator.cc
    process/process_manager.cc
    run_config/run_config.cc
)
target_include_directories(navigator_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(navigator_core PUBLIC logging Threads::Threads ${CMAKE_DL_LIBS})
if(BUILD_COCKPIT_UI)
    target_compile_definitions(navigator_core PUBLIC COCKPIT_HAS_HMI=1)
endif()

add_executable(cockpit-navigator main.cc)
target_link_libraries(cockpit-navigator PRIVATE navigator_core runtime)
