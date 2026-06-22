add_cockpit_library(audio_service audio_service.cc)
target_link_libraries(audio_service PUBLIC alsa_audio audio config voice)

add_executable(audio-service
    audio_grpc_service.cc
    main.cc
)
target_link_libraries(audio-service PRIVATE audio_service contracts logging runtime)
