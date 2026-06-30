add_executable(cockpit_smoke_test smoke_test.cc)
target_link_libraries(cockpit_smoke_test PRIVATE can vehicle)
add_test(NAME cockpit_smoke_test COMMAND cockpit_smoke_test)

add_executable(audio_wav_test audio_wav_test.cc)
target_link_libraries(audio_wav_test PRIVATE audio)
add_test(NAME audio_wav_test COMMAND audio_wav_test)

find_package(Threads REQUIRED)
add_executable(audio_frame_buffer_test audio_frame_buffer_test.cc)
target_link_libraries(audio_frame_buffer_test PRIVATE audio Threads::Threads)
add_test(NAME audio_frame_buffer_test COMMAND audio_frame_buffer_test)

add_executable(event_queue_test event_queue_test.cc)
target_link_libraries(event_queue_test PRIVATE event Threads::Threads)
add_test(NAME event_queue_test COMMAND event_queue_test)

add_executable(camera_frame_test camera_frame_test.cc)
target_link_libraries(camera_frame_test PRIVATE camera)
add_test(NAME camera_frame_test COMMAND camera_frame_test)

add_executable(latest_frame_buffer_test latest_frame_buffer_test.cc)
target_link_libraries(latest_frame_buffer_test PRIVATE camera)
add_test(NAME latest_frame_buffer_test COMMAND latest_frame_buffer_test)

add_executable(camera_service_test camera_service_test.cc)
target_link_libraries(camera_service_test PRIVATE camera_service)
add_test(NAME camera_service_test COMMAND camera_service_test)

add_executable(audio_capture_stream_test audio_capture_stream_test.cc)
target_link_libraries(audio_capture_stream_test PRIVATE audio)
add_test(NAME audio_capture_stream_test COMMAND audio_capture_stream_test)

add_executable(energy_vad_test energy_vad_test.cc)
target_link_libraries(energy_vad_test PRIVATE audio)
add_test(NAME energy_vad_test COMMAND energy_vad_test)

add_executable(speech_segmenter_test speech_segmenter_test.cc)
target_link_libraries(speech_segmenter_test PRIVATE audio)
add_test(NAME speech_segmenter_test COMMAND speech_segmenter_test)

add_executable(mock_speech_recognizer_test mock_speech_recognizer_test.cc)
target_link_libraries(mock_speech_recognizer_test PRIVATE voice)
add_test(NAME mock_speech_recognizer_test COMMAND mock_speech_recognizer_test)

add_executable(mock_voice_assistant_test mock_voice_assistant_test.cc)
target_link_libraries(mock_voice_assistant_test PRIVATE voice)
add_test(NAME mock_voice_assistant_test COMMAND mock_voice_assistant_test)

add_executable(cockpit_action_dispatcher_test cockpit_action_dispatcher_test.cc)
target_link_libraries(cockpit_action_dispatcher_test PRIVATE voice)
add_test(NAME cockpit_action_dispatcher_test COMMAND cockpit_action_dispatcher_test)

add_executable(audio_service_test audio_service_test.cc)
target_link_libraries(audio_service_test PRIVATE audio_service)
add_test(NAME audio_service_test COMMAND audio_service_test)

add_executable(audio_asr_pipeline_test audio_asr_pipeline_test.cc)
target_link_libraries(audio_asr_pipeline_test PRIVATE audio_service voice)
add_test(NAME audio_asr_pipeline_test COMMAND audio_asr_pipeline_test)

add_executable(voice_interaction_service_test voice_interaction_service_test.cc)
target_link_libraries(voice_interaction_service_test PRIVATE voice_interaction)
add_test(NAME voice_interaction_service_test COMMAND voice_interaction_service_test)

add_executable(speech_output_test speech_output_test.cc)
target_link_libraries(speech_output_test PRIVATE speech_output)
add_test(NAME speech_output_test COMMAND speech_output_test)

add_executable(system_config_test system_config_test.cc)
target_link_libraries(system_config_test PRIVATE config)
target_compile_definitions(system_config_test
    PRIVATE
        VALID_CONFIG_PATH="${PROJECT_SOURCE_DIR}/configs/config.yaml"
        INVALID_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config.yaml"
        INVALID_AUDIO_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_audio_config.yaml"
        INVALID_VAD_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_vad_config.yaml"
        INVALID_VOICE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_voice_config.yaml"
)
add_test(NAME system_config_test COMMAND system_config_test)

if(TARGET ui_model)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_executable(vehicle_state_model_test vehicle_state_model_test.cc)
    set_target_properties(vehicle_state_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(vehicle_state_model_test PRIVATE Qt6::Test ui_model)
    add_test(NAME vehicle_state_model_test COMMAND vehicle_state_model_test)
endif()
