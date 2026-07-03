add_cockpit_library(voice_hmi
    local_hmi_command_provider.cc
)
target_link_libraries(voice_hmi PUBLIC voice)
