add_library(safe_ota_commands STATIC safe_ota.cc)
target_include_directories(safe_ota_commands PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(safe_ota_commands PRIVATE
    diagnostic_cli
    navigator_core
    runtime
    upgrader_runtime
)

add_executable(safe-ota main.cc)
target_link_libraries(safe-ota PRIVATE safe_ota_commands)
