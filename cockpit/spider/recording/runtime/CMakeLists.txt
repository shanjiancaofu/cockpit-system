add_library(recording_service STATIC ${PROJECT_SOURCE_DIR}/cockpit/spider/recording/runtime/recording_service.cc)
target_link_libraries(recording_service PUBLIC contracts logging recording)

add_library(recording_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/spider/recording/runtime/recording_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/spider/recording/runtime/recording_runtime.cc
    ${PROJECT_SOURCE_DIR}/cockpit/spider/recording/runtime/vehicle_state_subscriber.cc
)
target_include_directories(recording_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(recording_runtime PUBLIC
    build_info
    config
    contracts
    logging
    recording_service
    time_util
)

