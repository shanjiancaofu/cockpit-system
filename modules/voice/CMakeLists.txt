add_subdirectory(asr)
add_subdirectory(tts)
add_subdirectory(assistant)
add_subdirectory(actions)
add_subdirectory(responses)

add_library(voice INTERFACE)
target_link_libraries(voice INTERFACE
    voice_actions
    voice_asr
    voice_assistant
    voice_responses
    voice_tts
)
