add_cockpit_library(voice_interaction voice_interaction_service.cc)
target_link_libraries(voice_interaction PUBLIC voice)

add_executable(voice-interaction-service
    audio_transcript_client.cc
    main.cc
    voice_grpc_service.cc
)
target_link_libraries(voice-interaction-service
    PRIVATE contracts logging runtime voice_interaction
)
