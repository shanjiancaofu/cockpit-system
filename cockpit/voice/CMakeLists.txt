add_subdirectory(domain)

add_library(agent_audio_stream STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/audio/audio_stream_client.cc)
target_include_directories(agent_audio_stream PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_audio_stream PUBLIC audio_stream_protocol)

add_library(agent_speech STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/asr/mock_speech_recognizer.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/kws/fixed_pcm_wake_prompt_player.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/kws/mock_wake_word_detector.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/kws/wake_prompt_player.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/pipeline/speech_pipeline.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/segment/speech_segmenter.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/tts/mock_speech_synthesizer.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/tts/speech_text_segmenter.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/vad/mock_voice_activity_detector.cc
)
target_include_directories(agent_speech PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_speech PUBLIC audio_frames audio_wav config time_util voice_assistant)

option(COCKPIT_ENABLE_SHERPA_AGENT "Build Jetson Agent product providers backed by Sherpa-ONNX" OFF)
if(COCKPIT_ENABLE_SHERPA_AGENT)
    set(COCKPIT_SHERPA_AGENT_RUNTIME_ROOT "" CACHE PATH "Prepared Sherpa-ONNX runtime root for Agent product builds")
    if(NOT COCKPIT_SHERPA_AGENT_RUNTIME_ROOT)
        message(FATAL_ERROR "COCKPIT_SHERPA_AGENT_RUNTIME_ROOT is required when COCKPIT_ENABLE_SHERPA_AGENT=ON")
    endif()
    add_library(sherpa-onnx-c-api SHARED IMPORTED GLOBAL)
    set_target_properties(sherpa-onnx-c-api PROPERTIES
        IMPORTED_LOCATION "${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT}/lib/libsherpa-onnx-c-api.so"
        INTERFACE_INCLUDE_DIRECTORIES "${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT}/include"
    )
    add_library(agent_sherpa_voice STATIC
        ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/providers/sherpa/sherpa_wake_word_detector.cc
        ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/providers/sherpa/sherpa_voice_activity_detector.cc
        ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/providers/sherpa/sherpa_sensevoice_recognizer.cc
        ${PROJECT_SOURCE_DIR}/cockpit/voice/speech/providers/sherpa/sherpa_kokoro_speech_synthesizer.cc
    )
    target_include_directories(agent_sherpa_voice
        PUBLIC
            ${PROJECT_SOURCE_DIR}
    )
    target_compile_definitions(agent_sherpa_voice PUBLIC COCKPIT_ENABLE_SHERPA_AGENT=1)
    target_link_libraries(agent_sherpa_voice PUBLIC agent_speech sherpa-onnx-c-api)
endif()

add_library(voice_interaction STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/voice/conversation/conversation_state_machine.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/interaction/voice_interaction_service.cc
)
target_include_directories(voice_interaction PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(voice_interaction PUBLIC event voice)
target_link_libraries(voice_interaction PUBLIC agent_llm)

add_library(voice_audio_clients STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/voice/audio/audio_focus_controller.cc
    ${PROJECT_SOURCE_DIR}/cockpit/voice/audio/audio_playback_client.cc
)
target_link_libraries(voice_audio_clients PUBLIC agent_speech contracts logging voice)

add_library(voice_vehicle_client STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/vehicle/gateway_vehicle_status_client.cc)
target_link_libraries(voice_vehicle_client PUBLIC contracts voice)

add_library(voice_hmi STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/hmi/local_hmi_command_provider.cc)
target_link_libraries(voice_hmi PUBLIC contracts voice PRIVATE logging)

add_library(voice_grpc STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/grpc/voice_grpc_service.cc)
target_link_libraries(voice_grpc PUBLIC contracts voice_interaction)
target_link_libraries(voice_grpc PRIVATE logging time_util)

add_library(agent_input_gate STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/runtime/voice_input_gate.cc)
target_include_directories(agent_input_gate PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_input_gate PUBLIC agent_speech voice_interaction)

add_library(agent_runtime STATIC ${PROJECT_SOURCE_DIR}/cockpit/voice/runtime/agent_runtime.cc)
target_include_directories(agent_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_runtime PUBLIC
    agent_audio_stream
    agent_input_gate
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
if(COCKPIT_ENABLE_SHERPA_AGENT)
    target_compile_definitions(agent_runtime PRIVATE COCKPIT_ENABLE_SHERPA_AGENT=1)
    target_link_libraries(agent_runtime PRIVATE agent_sherpa_voice)
endif()
