add_library(vehicle_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/vehicle_data_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/vehicle_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/vehicle_runtime.cc
)
target_include_directories(vehicle_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(vehicle_runtime PUBLIC can config contracts logging vehicle)

add_library(socketcan_chassis_sink STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/socketcan_chassis_sink.cc
)
target_include_directories(socketcan_chassis_sink PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(socketcan_chassis_sink PUBLIC socketcan vehicle)

add_library(vcan_chassis_safety_loop STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/vcan_chassis_safety_loop.cc
)
target_include_directories(vcan_chassis_safety_loop PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(vcan_chassis_safety_loop PUBLIC socketcan_chassis_sink vehicle)

