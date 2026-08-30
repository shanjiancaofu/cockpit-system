add_library(vehicle STATIC
    chassis_safety_adapter.cc
    chassis_can_safety_state_source.cc
    chassis_can_codec.cc
    chassis_client.cc
    chassis_state.cc
    vehicle_state.cc
    vehicle_can_codec.cc
)

target_link_libraries(vehicle PUBLIC can json_util time_util)
