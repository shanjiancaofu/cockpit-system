add_cockpit_library(voice_audio_clients
    audio_speech_client.cc
    audio_transcript_client.cc
)
target_link_libraries(voice_audio_clients PUBLIC contracts voice)
