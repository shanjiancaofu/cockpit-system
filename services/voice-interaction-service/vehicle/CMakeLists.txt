add_cockpit_library(voice_vehicle_client
    gateway_vehicle_status_client.cc
)
target_link_libraries(voice_vehicle_client PUBLIC contracts voice)
