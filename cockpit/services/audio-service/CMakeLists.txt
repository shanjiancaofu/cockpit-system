add_cockpit_library(audio_capture_module capture/audio_capture_module.cc)
target_link_libraries(audio_capture_module PUBLIC audio_capture runtime)

add_cockpit_library(speech_output playback/speech_output.cc)
target_link_libraries(speech_output PUBLIC audio_playback voice_responses voice_tts)
target_link_libraries(speech_output PRIVATE logging)

add_cockpit_library(audio_processing processing/audio_service.cc)
target_link_libraries(audio_processing PUBLIC
    alsa_audio audio_capture_module audio_vad config voice_asr voice_assistant
)

add_cockpit_library(audio_grpc grpc/audio_grpc_service.cc)
target_link_libraries(audio_grpc PUBLIC audio_processing contracts speech_output)
target_link_libraries(audio_grpc PRIVATE logging utils)

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
