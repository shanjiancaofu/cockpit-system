add_cockpit_library(voice
    mock_speech_recognizer.cc
)
target_link_libraries(voice PUBLIC audio)
