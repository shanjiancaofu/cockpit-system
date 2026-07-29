configure_file(build/build_info_config.h.in generated/build_info_config.h @ONLY)

add_library(base INTERFACE)
target_include_directories(base INTERFACE ${PROJECT_SOURCE_DIR})

add_library(build_info STATIC build/build_info.cc)
target_include_directories(build_info PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_library(config STATIC config/system_config.cc)
target_link_libraries(config PUBLIC yaml-cpp)

add_library(event STATIC event/event_queue.cc event/message_bus.cc)
target_link_libraries(event PUBLIC base time_util)

add_library(service_health STATIC health/service_health.cc)
target_link_libraries(service_health PUBLIC contracts)

add_library(ipc STATIC ipc/shared_memory_region.cc)
add_library(json_util STATIC json/json.cc)
add_library(logging STATIC logging/logger.cc)
target_link_libraries(logging PUBLIC Threads::Threads)
add_library(time_util STATIC time/time.cc)

add_library(runtime STATIC
    runtime/args.cc
    runtime/module.cc
    runtime/module_manager.cc
    runtime/process_runtime.cc
)
target_link_libraries(runtime PUBLIC base config logging)
