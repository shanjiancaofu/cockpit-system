add_subdirectory(interaction)
add_subdirectory(audio)
add_subdirectory(vehicle)
add_subdirectory(hmi)
add_subdirectory(grpc)

add_executable(voice-interaction-service main.cc)
target_link_libraries(voice-interaction-service
    PRIVATE
        contracts
        logging
        recording_event_client
        runtime
        voice
        voice_audio_clients
        voice_grpc
        voice_hmi
        voice_interaction
        voice_vehicle_client
)
