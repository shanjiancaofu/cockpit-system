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

add_executable(message_bus_test message_bus_test.cc)
target_link_libraries(message_bus_test PRIVATE event)
add_test(NAME message_bus_test COMMAND message_bus_test)

add_executable(module_manager_test module_manager_test.cc)
target_link_libraries(module_manager_test PRIVATE runtime)
add_test(NAME module_manager_test COMMAND module_manager_test)

add_executable(dependency_graph_test dependency_graph_test.cc)
target_link_libraries(dependency_graph_test PRIVATE runtime)
add_test(NAME dependency_graph_test COMMAND dependency_graph_test)

add_executable(shared_memory_region_test shared_memory_region_test.cc)
target_link_libraries(shared_memory_region_test PRIVATE ipc)
add_test(NAME shared_memory_region_test COMMAND shared_memory_region_test)

add_executable(json_util_test json_util_test.cc)
target_link_libraries(json_util_test PRIVATE json_util)
add_test(NAME json_util_test COMMAND json_util_test)

add_executable(service_health_test service_health_test.cc)
target_link_libraries(service_health_test PRIVATE service_health)
add_test(NAME service_health_test COMMAND service_health_test)

add_executable(cli_output_test cli_output_test.cc)
target_link_libraries(cli_output_test PRIVATE diagnostic_cli json_util)
add_test(NAME cli_output_test COMMAND cli_output_test)

add_executable(recording_session_test recording_session_test.cc)
target_link_libraries(recording_session_test PRIVATE recording)
add_test(NAME recording_session_test COMMAND recording_session_test)

add_executable(recording_catalog_test recording_catalog_test.cc)
target_link_libraries(recording_catalog_test PRIVATE recording)
add_test(NAME recording_catalog_test COMMAND recording_catalog_test)

add_executable(recording_timeline_test recording_timeline_test.cc)
target_link_libraries(recording_timeline_test PRIVATE recording)
add_test(NAME recording_timeline_test COMMAND recording_timeline_test)

add_executable(recording_integrity_test recording_integrity_test.cc)
target_link_libraries(recording_integrity_test PRIVATE recording)
add_test(NAME recording_integrity_test COMMAND recording_integrity_test)

add_executable(recording_service_test recording_service_test.cc)
target_link_libraries(recording_service_test PRIVATE recording_service)
add_test(NAME recording_service_test COMMAND recording_service_test)

add_executable(camera_frame_test camera_frame_test.cc)
target_link_libraries(camera_frame_test PRIVATE camera)
add_test(NAME camera_frame_test COMMAND camera_frame_test)

add_executable(latest_frame_buffer_test latest_frame_buffer_test.cc)
target_link_libraries(latest_frame_buffer_test PRIVATE camera)
add_test(NAME latest_frame_buffer_test COMMAND latest_frame_buffer_test)

add_executable(shared_frame_buffer_test shared_frame_buffer_test.cc)
target_link_libraries(shared_frame_buffer_test PRIVATE camera_shm)
add_test(NAME shared_frame_buffer_test COMMAND shared_frame_buffer_test)

add_executable(camera_service_test camera_service_test.cc)
target_link_libraries(camera_service_test PRIVATE camera_service)
add_test(NAME camera_service_test COMMAND camera_service_test)

add_executable(camera_synthetic_stability_test camera_synthetic_stability_test.cc)
target_link_libraries(camera_synthetic_stability_test PRIVATE camera_control synthetic_camera)
add_test(NAME camera_synthetic_stability_test COMMAND camera_synthetic_stability_test)

add_executable(camera_photo_service_test camera_photo_service_test.cc)
target_link_libraries(camera_photo_service_test PRIVATE camera_photo_service)
add_test(NAME camera_photo_service_test COMMAND camera_photo_service_test)

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

if(TARGET whisper_asr AND WHISPER_CPP_MODEL_PATH AND EXISTS "${WHISPER_CPP_MODEL_PATH}")
    add_executable(whisper_speech_recognizer_test whisper_speech_recognizer_test.cc)
    target_link_libraries(whisper_speech_recognizer_test PRIVATE audio whisper_asr)
    target_compile_definitions(whisper_speech_recognizer_test
        PRIVATE
            WHISPER_TEST_MODEL_PATH="${WHISPER_CPP_MODEL_PATH}"
            WHISPER_TEST_WAV_PATH="${WHISPER_CPP_DIR}/samples/jfk.wav"
    )
    add_test(NAME whisper_speech_recognizer_test COMMAND whisper_speech_recognizer_test)
endif()

add_executable(mock_voice_assistant_test mock_voice_assistant_test.cc)
target_link_libraries(mock_voice_assistant_test PRIVATE voice)
add_test(NAME mock_voice_assistant_test COMMAND mock_voice_assistant_test)

add_executable(async_voice_response_sink_test async_voice_response_sink_test.cc)
target_link_libraries(async_voice_response_sink_test PRIVATE voice Threads::Threads)
add_test(NAME async_voice_response_sink_test COMMAND async_voice_response_sink_test)

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
        INVALID_AUDIO_FORMAT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_audio_format_config.yaml"
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

    add_executable(camera_frame_model_test camera_frame_model_test.cc)
    set_target_properties(camera_frame_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(camera_frame_model_test PRIVATE Qt6::Test ui_camera)
    add_test(NAME camera_frame_model_test COMMAND camera_frame_model_test)
endif()
