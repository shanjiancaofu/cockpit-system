add_cockpit_library(voice_asr
    mock_speech_recognizer.cc
)
target_link_libraries(voice_asr PUBLIC audio)

if(TARGET whisper)
    add_cockpit_library(whisper_asr
        whisper_speech_recognizer.cc
    )
    target_link_libraries(whisper_asr PUBLIC audio whisper)
endif()
