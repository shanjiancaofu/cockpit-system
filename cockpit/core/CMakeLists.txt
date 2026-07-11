find_package(yaml-cpp REQUIRED)

configure_file(build/build_info_config.h.in generated/build_info_config.h @ONLY)

add_library(base INTERFACE)
target_include_directories(base INTERFACE ${PROJECT_SOURCE_DIR})

add_cockpit_library(build_info build/build_info.cc)
target_include_directories(build_info PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_cockpit_library(config config/system_config.cc)
target_link_libraries(config PUBLIC yaml-cpp)

add_cockpit_library(event event/event_queue.cc event/message_bus.cc)
target_link_libraries(event PUBLIC base utils)

add_cockpit_library(service_health health/service_health.cc)
target_link_libraries(service_health PUBLIC contracts)

add_cockpit_library(ipc ipc/shared_memory_region.cc)
add_cockpit_library(json_util json/json.cc)
add_cockpit_library(logging logging/logger.cc)
add_cockpit_library(utils utils/time.cc)

add_cockpit_library(runtime
    runtime/args.cc
    runtime/dependency_graph.cc
    runtime/module.cc
    runtime/module_manager.cc
    runtime/service_runtime.cc
)
target_link_libraries(runtime PUBLIC base config logging)
