add_executable(cockpit_smoke_test integration/smoke_test.cc)
target_link_libraries(cockpit_smoke_test PRIVATE can vehicle)
add_test(NAME cockpit_smoke_test COMMAND cockpit_smoke_test)

add_library(navigator_crash_module MODULE runtime/crash_module.cc)
target_include_directories(navigator_crash_module PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(navigator_crash_module PRIVATE Threads::Threads)

add_library(navigator_good_module MODULE runtime/good_module.cc)
target_include_directories(navigator_good_module PRIVATE ${PROJECT_SOURCE_DIR})

add_library(navigator_incompatible_module MODULE runtime/incompatible_module.cc)
target_include_directories(navigator_incompatible_module PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(navigator_test runtime/navigator_test.cc)
target_link_libraries(navigator_test PRIVATE navigator_core)
add_test(NAME navigator_test COMMAND navigator_test
    $<TARGET_FILE:cockpit-navigator>
    $<TARGET_FILE:navigator_good_module>
    $<TARGET_FILE:navigator_crash_module>
    $<TARGET_FILE:navigator_incompatible_module>
    $<TARGET_FILE:cockpit-ctl>
)

add_executable(crash_reporter_test runtime/crash_reporter_test.cc)
target_link_libraries(crash_reporter_test PRIVATE navigator_core)
add_test(NAME crash_reporter_test COMMAND crash_reporter_test)

add_executable(run_config_test runtime/run_config_test.cc)
target_link_libraries(run_config_test PRIVATE navigator_core)
add_test(NAME run_config_test COMMAND run_config_test)

add_test(NAME safe_ota_test COMMAND bash
    ${PROJECT_SOURCE_DIR}/scripts/tests/safe-ota.sh
    $<TARGET_FILE:safe-ota>
    $<TARGET_FILE:cockpit-navigator>
    ${CMAKE_BINARY_DIR}/lib/cockpit/modules
    ${PROJECT_SOURCE_DIR}
)
set_tests_properties(safe_ota_test PROPERTIES LABELS "system-grpc")

add_test(NAME navigator_stability_test COMMAND bash
    ${PROJECT_SOURCE_DIR}/scripts/tests/navigator-stability.sh
    --build-dir ${CMAKE_BINARY_DIR}
    --config ${PROJECT_SOURCE_DIR}/configs/development.yaml
    --duration 3
    --interval 1
    --fault crash
    --module camera_driver
    --fault-count 3
    --output ${CMAKE_BINARY_DIR}/navigator-stability-test.json
)
set_tests_properties(navigator_stability_test PROPERTIES TIMEOUT 30)
set_tests_properties(navigator_stability_test PROPERTIES LABELS "system-grpc")

add_test(NAME hmi_runtime_test COMMAND bash
    ${PROJECT_SOURCE_DIR}/scripts/tests/hmi-runtime.sh
    $<TARGET_FILE:cockpit-navigator>
    $<TARGET_FILE:cockpit-ctl>
    ${CMAKE_BINARY_DIR}/lib/cockpit/modules
    ${PROJECT_SOURCE_DIR}/configs/development.yaml
)
set_tests_properties(hmi_runtime_test PROPERTIES TIMEOUT 30)

add_executable(audio_wav_test modules/audio/audio_wav_test.cc)
target_link_libraries(audio_wav_test PRIVATE audio)
add_test(NAME audio_wav_test COMMAND audio_wav_test)

add_executable(audio_frame_buffer_test modules/audio/audio_frame_buffer_test.cc)
target_link_libraries(audio_frame_buffer_test PRIVATE audio Threads::Threads)
add_test(NAME audio_frame_buffer_test COMMAND audio_frame_buffer_test)

add_executable(audio_stream_transport_test modules/audio/audio_stream_transport_test.cc)
target_link_libraries(audio_stream_transport_test PRIVATE
    agent_audio_stream
    audio_stream_protocol
    audio_stream_publisher
)
add_test(NAME audio_stream_transport_test COMMAND audio_stream_transport_test)

add_executable(event_queue_test core/event_queue_test.cc)
target_link_libraries(event_queue_test PRIVATE event Threads::Threads)
add_test(NAME event_queue_test COMMAND event_queue_test)

add_executable(message_bus_test core/message_bus_test.cc)
target_link_libraries(message_bus_test PRIVATE event)
add_test(NAME message_bus_test COMMAND message_bus_test)

add_executable(module_manager_test core/module_manager_test.cc)
target_link_libraries(module_manager_test PRIVATE runtime)
add_test(NAME module_manager_test COMMAND module_manager_test)

add_executable(shared_memory_region_test core/shared_memory_region_test.cc)
target_link_libraries(shared_memory_region_test PRIVATE ipc)
add_test(NAME shared_memory_region_test COMMAND shared_memory_region_test)

add_executable(json_util_test core/json_util_test.cc)
target_link_libraries(json_util_test PRIVATE json_util)
add_test(NAME json_util_test COMMAND json_util_test)

add_executable(logger_test core/logger_test.cc)
target_link_libraries(logger_test PRIVATE logging)
add_test(NAME logger_test COMMAND logger_test)

add_executable(service_health_test core/service_health_test.cc)
target_link_libraries(service_health_test PRIVATE service_health)
add_test(NAME service_health_test COMMAND service_health_test)

add_executable(cli_output_test tools/cli_output_test.cc)
target_link_libraries(cli_output_test PRIVATE diagnostic_cli json_util)
add_test(NAME cli_output_test COMMAND cli_output_test)

add_test(NAME diagnostic_snapshot_test COMMAND bash
    ${PROJECT_SOURCE_DIR}/scripts/tests/diagnostic-snapshot.sh
    $<TARGET_FILE:cockpit-ctl>
    ${PROJECT_SOURCE_DIR}/configs/development.yaml
)
set_tests_properties(diagnostic_snapshot_test PROPERTIES LABELS "system-grpc")

add_executable(gateway_grpc_service_test library/transfer/gateway_grpc_service_test.cc)
target_link_libraries(gateway_grpc_service_test PRIVATE transfer_runtime)
add_test(NAME gateway_grpc_service_test COMMAND gateway_grpc_service_test)
set_tests_properties(gateway_grpc_service_test PROPERTIES LABELS "system-grpc")

add_executable(recording_session_test modules/recording/recording_session_test.cc)
target_link_libraries(recording_session_test PRIVATE recording)
add_test(NAME recording_session_test COMMAND recording_session_test)

add_executable(recording_catalog_test modules/recording/recording_catalog_test.cc)
target_link_libraries(recording_catalog_test PRIVATE recording)
add_test(NAME recording_catalog_test COMMAND recording_catalog_test)

add_executable(recording_timeline_test modules/recording/recording_timeline_test.cc)
target_link_libraries(recording_timeline_test PRIVATE recording)
add_test(NAME recording_timeline_test COMMAND recording_timeline_test)

add_executable(recording_integrity_test modules/recording/recording_integrity_test.cc)
target_link_libraries(recording_integrity_test PRIVATE recording)
add_test(NAME recording_integrity_test COMMAND recording_integrity_test)

add_executable(recording_service_test library/recording/recording_service_test.cc)
target_link_libraries(recording_service_test PRIVATE recording_service)
add_test(NAME recording_service_test COMMAND recording_service_test)

add_executable(camera_frame_test modules/camera/camera_frame_test.cc)
target_link_libraries(camera_frame_test PRIVATE camera)
add_test(NAME camera_frame_test COMMAND camera_frame_test)

add_executable(latest_frame_buffer_test modules/camera/latest_frame_buffer_test.cc)
target_link_libraries(latest_frame_buffer_test PRIVATE camera)
add_test(NAME latest_frame_buffer_test COMMAND latest_frame_buffer_test)

add_executable(shared_frame_buffer_test modules/camera/shared_frame_buffer_test.cc)
target_link_libraries(shared_frame_buffer_test PRIVATE camera_shm)
add_test(NAME shared_frame_buffer_test COMMAND shared_frame_buffer_test)

add_executable(camera_service_test library/driver/camera/camera_service_test.cc)
target_link_libraries(camera_service_test PRIVATE camera_service)
add_test(NAME camera_service_test COMMAND camera_service_test)

add_executable(camera_synthetic_stability_test
    library/driver/camera/camera_synthetic_stability_test.cc
)
target_link_libraries(camera_synthetic_stability_test PRIVATE camera_control synthetic_camera)
add_test(NAME camera_synthetic_stability_test COMMAND camera_synthetic_stability_test)

add_executable(camera_photo_service_test library/driver/camera/camera_photo_service_test.cc)
target_link_libraries(camera_photo_service_test PRIVATE camera_photo_service)
add_test(NAME camera_photo_service_test COMMAND camera_photo_service_test)

add_executable(audio_capture_stream_test modules/audio/audio_capture_stream_test.cc)
target_link_libraries(audio_capture_stream_test PRIVATE audio)
add_test(NAME audio_capture_stream_test COMMAND audio_capture_stream_test)

add_executable(audio_level_meter_test modules/audio/audio_level_meter_test.cc)
target_link_libraries(audio_level_meter_test PRIVATE audio)
add_test(NAME audio_level_meter_test COMMAND audio_level_meter_test)

add_executable(speech_segmenter_test agent/speech_segmenter_test.cc)
target_link_libraries(speech_segmenter_test PRIVATE agent_speech)
add_test(NAME speech_segmenter_test COMMAND speech_segmenter_test)

add_executable(mock_speech_recognizer_test agent/mock_speech_recognizer_test.cc)
target_link_libraries(mock_speech_recognizer_test PRIVATE agent_speech)
add_test(NAME mock_speech_recognizer_test COMMAND mock_speech_recognizer_test)

add_executable(mock_voice_assistant_test modules/voice/mock_voice_assistant_test.cc)
target_link_libraries(mock_voice_assistant_test PRIVATE voice)
add_test(NAME mock_voice_assistant_test COMMAND mock_voice_assistant_test)

add_executable(async_voice_response_sink_test modules/voice/async_voice_response_sink_test.cc)
target_link_libraries(async_voice_response_sink_test PRIVATE voice Threads::Threads)
add_test(NAME async_voice_response_sink_test COMMAND async_voice_response_sink_test)

add_executable(cockpit_action_dispatcher_test modules/voice/cockpit_action_dispatcher_test.cc)
target_link_libraries(cockpit_action_dispatcher_test PRIVATE voice)
add_test(NAME cockpit_action_dispatcher_test COMMAND cockpit_action_dispatcher_test)

add_executable(audio_capture_controller_test
    library/driver/audio/audio_capture_controller_test.cc
)
target_link_libraries(audio_capture_controller_test PRIVATE audio_capture_controller)
add_test(NAME audio_capture_controller_test COMMAND audio_capture_controller_test)

add_executable(speech_pipeline_test agent/speech_pipeline_test.cc)
target_link_libraries(speech_pipeline_test PRIVATE agent_speech)
add_test(NAME speech_pipeline_test COMMAND speech_pipeline_test)

add_executable(voice_interaction_service_test agent/voice_interaction_service_test.cc)
target_link_libraries(voice_interaction_service_test PRIVATE voice_interaction)
add_test(NAME voice_interaction_service_test COMMAND voice_interaction_service_test)

add_executable(conversation_state_machine_test agent/conversation_state_machine_test.cc)
target_link_libraries(conversation_state_machine_test PRIVATE voice_interaction)
add_test(NAME conversation_state_machine_test COMMAND conversation_state_machine_test)

add_executable(audio_playback_test library/driver/audio/audio_playback_test.cc)
target_link_libraries(audio_playback_test PRIVATE agent_speech audio_device_playback)
add_test(NAME audio_playback_test COMMAND audio_playback_test)

add_executable(system_config_test core/system_config_test.cc)
target_link_libraries(system_config_test PRIVATE config)
target_compile_definitions(system_config_test
    PRIVATE
        VALID_CONFIG_PATH="${PROJECT_SOURCE_DIR}/configs/development.yaml"
        PRODUCTION_CONFIG_PATH="${PROJECT_SOURCE_DIR}/configs/production.yaml"
        INVALID_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config.yaml"
        INVALID_CONFIG_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config_type.yaml"
        INVALID_CONFIG_KEY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config_key.yaml"
        INVALID_AUDIO_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_audio_config.yaml"
        INVALID_AUDIO_FORMAT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_audio_format_config.yaml"
)
add_test(NAME system_config_test COMMAND system_config_test)

if(TARGET ui_model)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_executable(vehicle_state_model_test apps/cockpit-ui/vehicle_state_model_test.cc)
    set_target_properties(vehicle_state_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(vehicle_state_model_test PRIVATE Qt6::Test ui_model)
    add_test(NAME vehicle_state_model_test COMMAND vehicle_state_model_test)

    add_executable(camera_frame_model_test apps/cockpit-ui/camera_frame_model_test.cc)
    set_target_properties(camera_frame_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(camera_frame_model_test PRIVATE Qt6::Test ui_camera)
    add_test(NAME camera_frame_model_test COMMAND camera_frame_model_test)

    add_executable(service_health_model_test apps/cockpit-ui/service_health_model_test.cc)
    set_target_properties(service_health_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(service_health_model_test PRIVATE Qt6::Test ui_health)
    add_test(NAME service_health_model_test COMMAND service_health_model_test)

    add_executable(hmi_control_test apps/cockpit-ui/hmi_control_test.cc)
    set_target_properties(hmi_control_test PROPERTIES AUTOMOC ON)
    target_link_libraries(hmi_control_test PRIVATE Qt6::Test ui_hmi voice_hmi)
    add_test(NAME hmi_control_test COMMAND hmi_control_test)
    set_tests_properties(hmi_control_test PROPERTIES LABELS "system-grpc")
endif()
