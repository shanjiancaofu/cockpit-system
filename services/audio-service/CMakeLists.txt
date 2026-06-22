add_cockpit_library(audio_service audio_service.cc)
target_link_libraries(audio_service PUBLIC alsa_audio audio config voice)

add_cockpit_library(speech_output speech_output.cc)
target_link_libraries(speech_output PUBLIC audio voice)
target_link_libraries(speech_output PRIVATE logging)

add_executable(audio-service
    audio_grpc_service.cc
    main.cc
)
target_link_libraries(audio-service
    PRIVATE alsa_audio audio_service contracts logging runtime speech_output
)
