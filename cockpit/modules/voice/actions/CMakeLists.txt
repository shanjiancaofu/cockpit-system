add_cockpit_library(voice_actions
    cockpit_action_dispatcher.cc
    hmi_command_provider.cc
    mock_action_dispatcher.cc
)
target_link_libraries(voice_actions PUBLIC voice_assistant)
