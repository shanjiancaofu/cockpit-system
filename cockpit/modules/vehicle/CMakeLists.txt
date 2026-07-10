add_cockpit_library(vehicle
    VehicleState.cc
    vehicle_can_codec.cc
)

target_link_libraries(vehicle PUBLIC can json_util utils)
