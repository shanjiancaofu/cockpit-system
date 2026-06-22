add_cockpit_library(voice
    cockpit_action_dispatcher.cc
    mock_action_dispatcher.cc
    mock_speech_synthesizer.cc
    mock_speech_recognizer.cc
    mock_voice_assistant.cc
)
target_link_libraries(voice PUBLIC audio)
