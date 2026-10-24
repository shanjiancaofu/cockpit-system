add_library(transfer_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/gateway_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/transfer_runtime.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/vehicle_state_client.cc
)
target_include_directories(transfer_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_runtime PUBLIC config contracts logging time_util)

