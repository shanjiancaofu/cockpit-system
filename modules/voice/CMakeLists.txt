add_cockpit_library(voice
    async_voice_response_sink.cc
    cockpit_action_dispatcher.cc
    hmi_command_provider.cc
    mock_action_dispatcher.cc
    mock_speech_synthesizer.cc
    mock_speech_recognizer.cc
    mock_voice_assistant.cc
)
target_link_libraries(voice PUBLIC audio)

if(TARGET whisper)
    add_cockpit_library(whisper_asr
        whisper_speech_recognizer.cc
    )
    target_link_libraries(whisper_asr PUBLIC audio whisper)
endif()
