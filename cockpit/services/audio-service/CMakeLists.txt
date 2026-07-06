add_subdirectory(capture)
add_subdirectory(processing)
add_subdirectory(playback)
add_subdirectory(grpc)

add_library(audio_service INTERFACE)
target_link_libraries(audio_service INTERFACE audio_processing)

add_executable(audio-service main.cc)
target_link_libraries(audio-service PRIVATE
    alsa_audio
    audio_grpc
    audio_processing
    logging
    runtime
    speech_output
)
if(TARGET whisper_asr)
    target_compile_definitions(audio-service PRIVATE COCKPIT_HAS_WHISPER_CPP_ASR=1)
    target_link_libraries(audio-service PRIVATE whisper_asr)
endif()
