add_library(sentinel_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/vanguard/sentinel/runtime/chassis_event_client.cc
    ${PROJECT_SOURCE_DIR}/cockpit/vanguard/sentinel/runtime/sentinel_actions.cc
    ${PROJECT_SOURCE_DIR}/cockpit/vanguard/sentinel/runtime/sentinel_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/vanguard/sentinel/runtime/sentinel_runtime.cc
)
target_include_directories(sentinel_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(sentinel_runtime PUBLIC
    config contracts json_util logging sentinel time_util
)

