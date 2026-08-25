add_library(vehicle STATIC
    chassis_can_codec.cc
    vehicle_state.cc
    vehicle_can_codec.cc
)

target_link_libraries(vehicle PUBLIC can json_util time_util)
