add_library(agent_audio_stream STATIC audio/audio_stream_client.cc)
target_include_directories(agent_audio_stream PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_audio_stream PUBLIC audio_stream_protocol)

add_library(agent_speech STATIC
    speech/asr/mock_speech_recognizer.cc
    speech/pipeline/speech_pipeline.cc
    speech/segment/speech_segmenter.cc
    speech/tts/mock_speech_synthesizer.cc
    speech/vad/mock_voice_activity_detector.cc
)
target_include_directories(agent_speech PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_speech PUBLIC audio_frames audio_wav config time_util voice_assistant)

add_library(voice_interaction STATIC
    conversation/conversation_state_machine.cc
    interaction/voice_interaction_service.cc
)
target_include_directories(voice_interaction PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(voice_interaction PUBLIC event voice)

add_library(voice_audio_clients STATIC audio/audio_playback_client.cc)
target_link_libraries(voice_audio_clients PUBLIC agent_speech contracts voice)

add_library(voice_vehicle_client STATIC vehicle/gateway_vehicle_status_client.cc)
target_link_libraries(voice_vehicle_client PUBLIC contracts voice)

add_library(voice_hmi STATIC hmi/local_hmi_command_provider.cc)
target_link_libraries(voice_hmi PUBLIC contracts voice)

add_library(voice_grpc STATIC grpc/voice_grpc_service.cc)
target_link_libraries(voice_grpc PUBLIC contracts voice_interaction)
target_link_libraries(voice_grpc PRIVATE logging time_util)

add_library(agent_runtime STATIC runtime/agent_runtime.cc)
target_include_directories(agent_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_runtime PUBLIC
    agent_audio_stream
    agent_speech
    config
    json_util
    logging
    recording_event_client
    voice
    voice_audio_clients
    voice_grpc
    voice_hmi
    voice_interaction
    voice_vehicle_client
)
