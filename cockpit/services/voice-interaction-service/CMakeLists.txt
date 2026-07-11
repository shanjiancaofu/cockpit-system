add_cockpit_library(voice_interaction interaction/voice_interaction_service.cc)
target_link_libraries(voice_interaction PUBLIC event voice)

add_cockpit_library(voice_audio_clients audio/audio_speech_client.cc audio/audio_transcript_client.cc)
target_link_libraries(voice_audio_clients PUBLIC contracts voice)

add_cockpit_library(voice_vehicle_client vehicle/gateway_vehicle_status_client.cc)
target_link_libraries(voice_vehicle_client PUBLIC contracts voice)

add_cockpit_library(voice_hmi hmi/local_hmi_command_provider.cc)
target_link_libraries(voice_hmi PUBLIC voice)

add_cockpit_library(voice_grpc grpc/voice_grpc_service.cc)
target_link_libraries(voice_grpc PUBLIC contracts voice_interaction)
target_link_libraries(voice_grpc PRIVATE logging utils)

add_executable(voice-interaction-service main.cc)
target_link_libraries(voice-interaction-service
    PRIVATE
        contracts
        json_util
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
