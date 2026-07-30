add_library(voice_assistant STATIC assistant/mock_voice_assistant.cc)

add_library(voice_actions STATIC
    actions/cockpit_action_dispatcher.cc
    actions/hmi_command_provider.cc
    actions/mock_action_dispatcher.cc
)
target_link_libraries(voice_actions PUBLIC voice_assistant)

add_library(voice_responses STATIC responses/async_voice_response_sink.cc)

add_library(voice INTERFACE)
target_link_libraries(voice INTERFACE
    voice_actions
    voice_assistant
    voice_responses
)
