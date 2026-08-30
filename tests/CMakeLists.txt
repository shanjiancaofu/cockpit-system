add_executable(cockpit_smoke_test integration/smoke_test.cc)
target_link_libraries(cockpit_smoke_test PRIVATE can vehicle)
add_test(NAME cockpit_smoke_test COMMAND cockpit_smoke_test)

add_executable(socket_can_adapter_test modules/can/socket_can_adapter_test.cc)
target_link_libraries(socket_can_adapter_test PRIVATE can)
add_test(NAME socket_can_adapter_test COMMAND socket_can_adapter_test)

add_library(navigator_crash_module MODULE runtime/crash_module.cc)
target_include_directories(navigator_crash_module PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(navigator_crash_module PRIVATE Threads::Threads)

add_library(navigator_good_module MODULE runtime/good_module.cc)
target_include_directories(navigator_good_module PRIVATE ${PROJECT_SOURCE_DIR})

add_library(navigator_debugger_good_module MODULE runtime/debugger_good_module.cc)
target_include_directories(navigator_debugger_good_module PRIVATE ${PROJECT_SOURCE_DIR})

add_library(navigator_calibration_good_module MODULE runtime/calibration_good_module.cc)
target_include_directories(navigator_calibration_good_module PRIVATE ${PROJECT_SOURCE_DIR})

add_library(navigator_incompatible_module MODULE runtime/incompatible_module.cc)
target_include_directories(navigator_incompatible_module PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(navigator_test runtime/navigator_test.cc)
target_link_libraries(navigator_test PRIVATE navigator_core)
add_test(NAME navigator_test COMMAND navigator_test
    $<TARGET_FILE:cockpit-navigator>
    $<TARGET_FILE:navigator_good_module>
    $<TARGET_FILE:navigator_debugger_good_module>
    $<TARGET_FILE:navigator_calibration_good_module>
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
if(COCKPIT_ENABLE_ROS2 AND DEFINED ENV{LD_LIBRARY_PATH})
    set_tests_properties(navigator_stability_test PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}"
    )
endif()

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

add_executable(time_test core/time_test.cc)
target_link_libraries(time_test PRIVATE time_util)
add_test(NAME time_test COMMAND time_test)

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

add_executable(vehicle_grpc_service_test library/driver/vehicle/vehicle_grpc_service_test.cc)
target_link_libraries(vehicle_grpc_service_test PRIVATE vehicle_runtime)
add_test(NAME vehicle_grpc_service_test COMMAND vehicle_grpc_service_test)
set_tests_properties(vehicle_grpc_service_test PROPERTIES LABELS "system-grpc")

add_executable(chassis_can_codec_test modules/vehicle/chassis_can_codec_test.cc)
target_link_libraries(chassis_can_codec_test PRIVATE vehicle)
add_test(NAME chassis_can_codec_test COMMAND chassis_can_codec_test)

add_executable(chassis_client_test modules/vehicle/chassis_client_test.cc)
target_link_libraries(chassis_client_test PRIVATE vehicle)
add_test(NAME chassis_client_test COMMAND chassis_client_test)

add_executable(chassis_safety_adapter_test modules/vehicle/chassis_safety_adapter_test.cc)
target_link_libraries(chassis_safety_adapter_test PRIVATE vehicle)
add_test(NAME chassis_safety_adapter_test COMMAND chassis_safety_adapter_test)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(vehicle_data_chassis_vcan_test
        library/driver/vehicle/vehicle_data_chassis_vcan_test.cc
    )
    target_link_libraries(vehicle_data_chassis_vcan_test PRIVATE vehicle_runtime)
    add_test(NAME vehicle_data_chassis_vcan_test COMMAND
        bash ${PROJECT_SOURCE_DIR}/scripts/tests/chassis-vehicle-vcan.sh
        $<TARGET_FILE:vehicle_data_chassis_vcan_test>
    )
    set_tests_properties(vehicle_data_chassis_vcan_test PROPERTIES
        LABELS "socketcan"
        SKIP_RETURN_CODE 77
    )

    add_executable(socketcan_chassis_sink_vcan_test
        library/driver/vehicle/socketcan_chassis_sink_vcan_test.cc
    )
    target_link_libraries(socketcan_chassis_sink_vcan_test PRIVATE socketcan_chassis_sink)
    add_test(NAME socketcan_chassis_sink_vcan_test COMMAND
        bash ${PROJECT_SOURCE_DIR}/scripts/tests/chassis-vehicle-vcan.sh
        $<TARGET_FILE:socketcan_chassis_sink_vcan_test>
    )
    set_tests_properties(socketcan_chassis_sink_vcan_test PROPERTIES
        LABELS "socketcan"
        SKIP_RETURN_CODE 77
    )
endif()
if(TARGET can-simulator)
    add_test(NAME chassis_can_simulator_stdout_test COMMAND
        $<TARGET_FILE:can-simulator>
        --backend stdout
        --protocol chassis
        --linear-mm-s 500
        --angular-mrad-s -250
        --samples 2
        --interval-ms 1
        --config ${PROJECT_SOURCE_DIR}/configs/development.yaml
    )
endif()

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

add_executable(media_service_test modules/media/media_service_test.cc)
target_link_libraries(media_service_test PRIVATE media_service)
add_test(NAME media_service_test COMMAND media_service_test)

add_executable(bridge_service_test modules/bridge/bridge_service_test.cc)
target_link_libraries(bridge_service_test PRIVATE bridge)
add_test(NAME bridge_service_test COMMAND bridge_service_test)

add_executable(bridge_grpc_service_test
    library/bridge/bridge_grpc_service_test.cc
)
target_link_libraries(bridge_grpc_service_test PRIVATE bridge_runtime)
add_test(NAME bridge_grpc_service_test COMMAND bridge_grpc_service_test)
set_tests_properties(bridge_grpc_service_test PROPERTIES LABELS "system-grpc")

if(COCKPIT_ENABLE_ROS2)
    add_executable(fake_nav2_action_server
        fixtures/fake_nav2_action_server.cc
    )
    ament_target_dependencies(fake_nav2_action_server
        geometry_msgs nav2_msgs rclcpp rclcpp_action
    )

    add_executable(bridge_ros2_integration_test
        library/bridge/bridge_ros2_integration_test.cc
    )
    target_link_libraries(bridge_ros2_integration_test bridge_ros2 Threads::Threads)
    ament_target_dependencies(bridge_ros2_integration_test
        builtin_interfaces geometry_msgs nav2_msgs rclcpp rclcpp_action sensor_msgs
    )
    add_test(NAME bridge_ros2_integration_test COMMAND bridge_ros2_integration_test)
    set_tests_properties(bridge_ros2_integration_test PROPERTIES
        LABELS "system-ros2"
        TIMEOUT 20
    )
    if(DEFINED ENV{LD_LIBRARY_PATH})
        set_tests_properties(bridge_ros2_integration_test PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}"
        )
    endif()
    add_test(
        NAME bridge_ros2_full_process_smoke
        COMMAND ${CMAKE_COMMAND} -E env BUILD_DIR=${CMAKE_BINARY_DIR}
                bash ${PROJECT_SOURCE_DIR}/scripts/tests/ros2-nav2-bridge-smoke.sh
    )
    set_tests_properties(bridge_ros2_full_process_smoke PROPERTIES
        LABELS "system-ros2"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )
endif()

add_executable(sentinel_service_test modules/sentinel/sentinel_service_test.cc)
target_link_libraries(sentinel_service_test PRIVATE sentinel)
add_test(NAME sentinel_service_test COMMAND sentinel_service_test)

add_executable(sentinel_fake_closure_test library/sentinel/sentinel_fake_closure_test.cc)
target_link_libraries(sentinel_fake_closure_test PRIVATE sentinel_runtime vehicle_runtime)
add_test(NAME sentinel_fake_closure_test COMMAND sentinel_fake_closure_test)
set_tests_properties(sentinel_fake_closure_test PROPERTIES LABELS "system-grpc")

add_executable(media_manifest_test modules/media/media_manifest_test.cc)
target_link_libraries(media_manifest_test PRIVATE media)
add_test(NAME media_manifest_test COMMAND media_manifest_test)

add_executable(gstreamer_media_player_test modules/media/gstreamer_media_player_test.cc)
target_link_libraries(gstreamer_media_player_test PRIVATE media)
add_test(NAME gstreamer_media_player_test COMMAND gstreamer_media_player_test)

add_executable(media_grpc_service_test library/media/media_grpc_service_test.cc)
target_link_libraries(media_grpc_service_test PRIVATE media_runtime)
add_test(NAME media_grpc_service_test COMMAND media_grpc_service_test)
set_tests_properties(media_grpc_service_test PROPERTIES LABELS "system-grpc")

add_executable(camera_frame_test modules/camera/camera_frame_test.cc)
target_link_libraries(camera_frame_test PRIVATE camera)
add_test(NAME camera_frame_test COMMAND camera_frame_test)

add_executable(raw10_unpack_test modules/camera/raw10_unpack_test.cc)
target_link_libraries(raw10_unpack_test PRIVATE camera_isp)
add_test(NAME raw10_unpack_test COMMAND raw10_unpack_test)

add_executable(camera_calibration_loader_test
    modules/hawkeye/camera_calibration_loader_test.cc
)
target_link_libraries(camera_calibration_loader_test PRIVATE hawkeye)
add_test(NAME camera_calibration_loader_test COMMAND camera_calibration_loader_test)

if(TARGET camera-calibrator)
add_test(NAME camera_calibrator_cli_test COMMAND bash
        ${PROJECT_SOURCE_DIR}/tests/tools/camera_calibrator_cli_test.sh
        $<TARGET_FILE:camera-calibrator>
    )
endif()

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

if(TARGET camera-calibrator)
    find_package(OpenCV REQUIRED COMPONENTS calib3d imgcodecs imgproc)
    add_executable(camera_calibrator_offline_test tools/camera_calibrator_offline_test.cc)
    target_include_directories(camera_calibrator_offline_test PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(camera_calibrator_offline_test PRIVATE ${OpenCV_LIBS})
    add_test(NAME camera_calibrator_offline_test COMMAND camera_calibrator_offline_test
        $<TARGET_FILE:camera-calibrator>
    )
    # Q12 positive and negative image-generation regressions take about 25 s in
    # Debug and can exceed 30 s under ASan instrumentation on shared CI runners.
    set_tests_properties(camera_calibrator_offline_test PROPERTIES TIMEOUT 90)
endif()
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

add_executable(deterministic_command_router_test
    modules/voice/deterministic_command_router_test.cc
)
target_link_libraries(deterministic_command_router_test PRIVATE voice_assistant)
add_test(NAME deterministic_command_router_test COMMAND deterministic_command_router_test)

add_executable(async_voice_response_sink_test modules/voice/async_voice_response_sink_test.cc)
target_link_libraries(async_voice_response_sink_test PRIVATE voice Threads::Threads)
add_test(NAME async_voice_response_sink_test COMMAND async_voice_response_sink_test)

add_executable(cockpit_action_dispatcher_test modules/voice/cockpit_action_dispatcher_test.cc)
target_link_libraries(cockpit_action_dispatcher_test PRIVATE voice)
add_test(NAME cockpit_action_dispatcher_test COMMAND cockpit_action_dispatcher_test)

add_test(
    NAME driver_dependency_boundary_test
    COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/tests/driver-dependency-boundaries.sh"
)

add_executable(action_provider_deadline_test agent/action_provider_deadline_test.cc)
target_link_libraries(action_provider_deadline_test PRIVATE voice_hmi voice_vehicle_client)
add_test(NAME action_provider_deadline_test COMMAND action_provider_deadline_test)
set_tests_properties(action_provider_deadline_test PROPERTIES LABELS "system-grpc")

add_executable(audio_capture_controller_test
    library/driver/audio/audio_capture_controller_test.cc
)
target_link_libraries(audio_capture_controller_test PRIVATE audio_capture_controller)
add_test(NAME audio_capture_controller_test COMMAND audio_capture_controller_test)

add_executable(speech_pipeline_test agent/speech_pipeline_test.cc)
target_link_libraries(speech_pipeline_test PRIVATE agent_speech)
add_test(NAME speech_pipeline_test COMMAND speech_pipeline_test)

add_executable(voice_input_gate_test agent/voice_input_gate_test.cc)
target_link_libraries(voice_input_gate_test PRIVATE agent_input_gate voice_interaction)
add_test(NAME voice_input_gate_test COMMAND voice_input_gate_test)

add_executable(voice_interaction_service_test agent/voice_interaction_service_test.cc)
target_link_libraries(voice_interaction_service_test PRIVATE voice_interaction)
add_test(NAME voice_interaction_service_test COMMAND voice_interaction_service_test)

add_executable(voice_pcm_e2e_test agent/voice_pcm_e2e_test.cc)
target_link_libraries(voice_pcm_e2e_test PRIVATE
    agent_audio_stream
    agent_speech
    audio_stream_publisher
    voice_interaction
)
add_test(NAME voice_pcm_e2e_test COMMAND voice_pcm_e2e_test)

add_executable(local_llm_client_test agent/local_llm_client_test.cc)
target_link_libraries(local_llm_client_test PRIVATE agent_llm voice_interaction voice)
add_test(NAME local_llm_client_test COMMAND local_llm_client_test)

add_executable(llama_server_local_llm_client_test
    agent/llama_server_local_llm_client_test.cc
)
target_link_libraries(llama_server_local_llm_client_test PRIVATE agent_llm)
add_test(NAME llama_server_local_llm_client_test COMMAND llama_server_local_llm_client_test)

add_executable(llama_server_live_smoke agent/llama_server_live_smoke.cc)
target_link_libraries(llama_server_live_smoke PRIVATE agent_llm)

add_executable(fake_llama_server agent/fake_llama_server.cc)
add_executable(llama_server_process_test agent/llama_server_process_test.cc)
target_link_libraries(llama_server_process_test PRIVATE agent_llm)
add_dependencies(llama_server_process_test fake_llama_server)
add_test(NAME llama_server_process_test
    COMMAND llama_server_process_test $<TARGET_FILE:fake_llama_server>
)

if(COCKPIT_ENABLE_SHERPA_AGENT)
    add_executable(sherpa_voice_smoke_test agent/sherpa_voice_smoke_test.cc)
    target_link_libraries(sherpa_voice_smoke_test PRIVATE agent_sherpa_voice voice)
    add_test(NAME sherpa_voice_smoke_test COMMAND sherpa_voice_smoke_test)
    set_tests_properties(sherpa_voice_smoke_test PROPERTIES
        ENVIRONMENT
            "COCKPIT_AI_ROOT=${PROJECT_SOURCE_DIR}/_output/ai;LD_LIBRARY_PATH=${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT}/lib"
    )
    add_executable(sherpa_voice_gate_smoke_test agent/sherpa_voice_gate_smoke_test.cc)
    target_link_libraries(sherpa_voice_gate_smoke_test PRIVATE agent_sherpa_voice agent_input_gate voice)
    add_test(NAME sherpa_voice_gate_smoke_test COMMAND sherpa_voice_gate_smoke_test)
    set_tests_properties(sherpa_voice_gate_smoke_test PROPERTIES
        ENVIRONMENT
            "COCKPIT_AI_ROOT=${PROJECT_SOURCE_DIR}/_output/ai;LD_LIBRARY_PATH=${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT}/lib"
    )
    add_executable(sherpa_tts_smoke_test agent/sherpa_tts_smoke_test.cc)
    target_link_libraries(sherpa_tts_smoke_test PRIVATE agent_sherpa_voice)
    add_test(NAME sherpa_tts_smoke_test COMMAND sherpa_tts_smoke_test)
    set_tests_properties(sherpa_tts_smoke_test PROPERTIES
        ENVIRONMENT
            "COCKPIT_AI_ROOT=${PROJECT_SOURCE_DIR}/_output/ai;LD_LIBRARY_PATH=${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT}/lib"
    )
    add_executable(sherpa_tts_stability_test agent/sherpa_tts_stability_test.cc)
    target_link_libraries(sherpa_tts_stability_test PRIVATE agent_sherpa_voice)
    add_executable(sherpa_tts_benchmark_test agent/sherpa_tts_benchmark_test.cc)
    target_link_libraries(sherpa_tts_benchmark_test PRIVATE agent_sherpa_voice agent_speech)
    add_executable(sherpa_asr_benchmark_test agent/sherpa_asr_benchmark_test.cc)
    target_link_libraries(sherpa_asr_benchmark_test PRIVATE agent_sherpa_voice voice_assistant)
endif()

add_executable(conversation_state_machine_test agent/conversation_state_machine_test.cc)
target_link_libraries(conversation_state_machine_test PRIVATE voice_interaction)
add_test(NAME conversation_state_machine_test COMMAND conversation_state_machine_test)

add_executable(audio_playback_test library/driver/audio/audio_playback_test.cc)
target_link_libraries(audio_playback_test PRIVATE agent_speech audio_device_playback)
add_test(NAME audio_playback_test COMMAND audio_playback_test)

add_executable(audio_playback_client_test agent/audio_playback_client_test.cc)
target_link_libraries(audio_playback_client_test PRIVATE voice_audio_clients)
add_test(NAME audio_playback_client_test COMMAND audio_playback_client_test)

add_executable(audio_focus_controller_test agent/audio_focus_controller_test.cc)
target_link_libraries(audio_focus_controller_test PRIVATE media_runtime voice_audio_clients)
add_test(NAME audio_focus_controller_test COMMAND audio_focus_controller_test)
set_tests_properties(audio_focus_controller_test PROPERTIES LABELS "system-grpc")

add_executable(speech_text_segmenter_test agent/speech_text_segmenter_test.cc)
target_link_libraries(speech_text_segmenter_test PRIVATE agent_speech)
add_test(NAME speech_text_segmenter_test COMMAND speech_text_segmenter_test)

add_executable(audio_playback_grpc_test library/driver/audio/audio_playback_grpc_test.cc)
target_link_libraries(audio_playback_grpc_test PRIVATE audio_grpc)
add_test(NAME audio_playback_grpc_test COMMAND audio_playback_grpc_test)
set_tests_properties(audio_playback_grpc_test PROPERTIES LABELS system-grpc)

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
        INVALID_CAMERA_PIPELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_camera_pipeline.yaml"
        INVALID_CAMERA_UVC_FORMAT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_camera_uvc_format.yaml"
        INVALID_LEGACY_CAMERA_BACKEND_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_legacy_camera_backend.yaml"
        INVALID_ZERO_VOICE_TIMEOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_zero_voice_timeout.yaml"
        INVALID_NEGATIVE_VOICE_TIMEOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_negative_voice_timeout.yaml"
        INVALID_LEGACY_REQUEST_TIMEOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_legacy_request_timeout.yaml"
)
add_test(NAME system_config_test COMMAND system_config_test)

if(TARGET ui_model)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Gui Qml Quick Test)
    add_executable(app_launcher_model_test apps/cockpit-ui/app_launcher_model_test.cc)
    set_target_properties(app_launcher_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(app_launcher_model_test PRIVATE Qt6::Test ui_app_launcher)
    add_test(NAME app_launcher_model_test COMMAND app_launcher_model_test)
    if(COCKPIT_ENABLE_TSAN)
        set_property(TEST app_launcher_model_test APPEND PROPERTY ENVIRONMENT
            "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/qt6-test-tsan.supp"
            "QTEST_FUNCTION_TIMEOUT=0"
        )
    endif()

    add_executable(media_control_model_test apps/cockpit-ui/media_control_model_test.cc)
    set_target_properties(media_control_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(media_control_model_test PRIVATE Qt6::Test ui_media)
    add_test(NAME media_control_model_test COMMAND media_control_model_test)
    if(COCKPIT_ENABLE_TSAN)
        set_property(TEST media_control_model_test APPEND PROPERTY ENVIRONMENT
            "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/qt6-test-tsan.supp"
            "QTEST_FUNCTION_TIMEOUT=0"
        )
    endif()

    add_executable(media_control_model_grpc_test
        apps/cockpit-ui/media_control_model_grpc_test.cc
    )
    set_target_properties(media_control_model_grpc_test PROPERTIES AUTOMOC ON)
    target_link_libraries(media_control_model_grpc_test PRIVATE Qt6::Test ui_media media_runtime)
    add_test(NAME media_control_model_grpc_test COMMAND media_control_model_grpc_test)
    set_tests_properties(media_control_model_grpc_test PROPERTIES LABELS "system-grpc")

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

    add_executable(voice_status_model_test apps/cockpit-ui/voice_status_model_test.cc)
    set_target_properties(voice_status_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(voice_status_model_test PRIVATE Qt6::Test ui_voice)
    add_test(NAME voice_status_model_test COMMAND voice_status_model_test)
    set_tests_properties(voice_status_model_test PROPERTIES LABELS "system-grpc")

    add_executable(sentinel_status_model_test apps/cockpit-ui/sentinel_status_model_test.cc)
    set_target_properties(sentinel_status_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(sentinel_status_model_test PRIVATE Qt6::Test ui_sentinel sentinel_runtime)
    add_test(NAME sentinel_status_model_test COMMAND sentinel_status_model_test)
    set_tests_properties(sentinel_status_model_test PROPERTIES LABELS "system-grpc")

    add_executable(hmi_control_test apps/cockpit-ui/hmi_control_test.cc)
    set_target_properties(hmi_control_test PROPERTIES AUTOMOC ON)
    target_link_libraries(hmi_control_test PRIVATE Qt6::Test ui_hmi ui_media voice_hmi media_runtime)
    add_test(NAME hmi_control_test COMMAND hmi_control_test)
    set_tests_properties(hmi_control_test PROPERTIES LABELS "system-grpc")

    add_executable(cockpit_ui_qml_test
        apps/cockpit-ui/cockpit_ui_qml_test.cc
        ${PROJECT_SOURCE_DIR}/cockpit/apps/cockpit-ui/qml.qrc
    )
    set_target_properties(cockpit_ui_qml_test PROPERTIES AUTOMOC ON AUTORCC ON)
    target_link_libraries(cockpit_ui_qml_test PRIVATE
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        ui_app_launcher
        ui_camera
        ui_health
        ui_hmi
        ui_media
        ui_sentinel
        ui_vehicle
        ui_voice
    )
    add_test(NAME cockpit_ui_qml_test COMMAND cockpit_ui_qml_test)
    set_tests_properties(cockpit_ui_qml_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )
    if(COCKPIT_ENABLE_ASAN_UBSAN)
        set_property(TEST cockpit_ui_qml_test APPEND PROPERTY ENVIRONMENT
            "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/qt6-offscreen-lsan.supp:print_suppressions=0"
        )
    endif()
endif()
