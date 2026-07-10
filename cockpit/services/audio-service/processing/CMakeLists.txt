add_cockpit_library(audio_processing audio_service.cc)
target_link_libraries(audio_processing PUBLIC
    alsa_audio
    audio_capture_module
    audio_vad
    config
    voice_asr
    voice_assistant
)
