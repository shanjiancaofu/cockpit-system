add_cockpit_library(voice_tts
    mock_speech_synthesizer.cc
)
target_link_libraries(voice_tts PUBLIC audio)
