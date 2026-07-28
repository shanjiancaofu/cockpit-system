add_library(transfer_runtime STATIC
    transfer/gateway_grpc_service.cc
    transfer/transfer_runtime.cc
    transfer/vehicle_state_client.cc
)
target_include_directories(transfer_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_runtime PUBLIC config contracts logging time_util)

add_library(vehicle_runtime STATIC
    driver/vehicle/vehicle_data_service.cc
    driver/vehicle/vehicle_grpc_service.cc
    driver/vehicle/vehicle_runtime.cc
)
target_include_directories(vehicle_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(vehicle_runtime PUBLIC config contracts logging socketcan vehicle)

add_cockpit_library(audio_capture_module driver/audio/capture/audio_capture_module.cc)
target_link_libraries(audio_capture_module PUBLIC audio_capture runtime)

add_cockpit_library(speech_output driver/audio/playback/speech_output.cc)
target_link_libraries(speech_output PUBLIC audio_playback voice_responses voice_tts)
target_link_libraries(speech_output PRIVATE logging)

add_cockpit_library(audio_processing driver/audio/processing/audio_service.cc)
target_link_libraries(audio_processing PUBLIC
    alsa_audio audio_capture_module audio_vad config voice_asr voice_assistant
)

add_cockpit_library(audio_grpc driver/audio/grpc/audio_grpc_service.cc)
target_link_libraries(audio_grpc PUBLIC audio_processing contracts speech_output)
target_link_libraries(audio_grpc PRIVATE logging time_util)

add_library(audio_runtime STATIC driver/audio/audio_runtime.cc)
target_include_directories(audio_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(audio_runtime PUBLIC
    alsa_audio
    audio_grpc
    audio_processing
    config
    logging
    sherpa_onnx_asr
    speech_output
)

add_cockpit_library(camera_preview driver/camera/preview/camera_preview_module.cc)
target_link_libraries(camera_preview PUBLIC camera_capture runtime)

add_cockpit_library(camera_control driver/camera/control/camera_service.cc)
target_link_libraries(camera_control PUBLIC base camera_frames camera_preview event runtime v4l2_camera)
target_link_libraries(camera_control PRIVATE gstreamer_camera json_util)

add_cockpit_library(camera_photo_service driver/camera/photo/camera_photo_service.cc)
target_link_libraries(camera_photo_service PUBLIC camera_photo camera_shm time_util)

add_cockpit_library(camera_grpc driver/camera/grpc/camera_grpc_service.cc)
target_link_libraries(camera_grpc PUBLIC
    camera_control
    camera_photo_service
    contracts
    recording_event_client
)
target_link_libraries(camera_grpc PRIVATE json_util logging time_util)

add_cockpit_library(camera_recording_bridge driver/camera/recording_bridge.cc)
target_link_libraries(camera_recording_bridge PUBLIC event)

add_library(camera_service INTERFACE)
target_link_libraries(camera_service INTERFACE camera_control camera_recording_bridge)

add_library(camera_runtime STATIC driver/camera/camera_runtime.cc)
target_include_directories(camera_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(camera_runtime PUBLIC
    camera_control
    camera_grpc
    camera_photo_service
    camera_recording_bridge
    camera_shm
    config
    logging
    recording_event_client
    synthetic_camera
)

add_cockpit_library(voice_interaction agent/interaction/voice_interaction_service.cc)
target_link_libraries(voice_interaction PUBLIC event voice)

add_cockpit_library(voice_audio_clients
    agent/audio/audio_speech_client.cc
    agent/audio/audio_transcript_client.cc
)
target_link_libraries(voice_audio_clients PUBLIC contracts voice)

add_cockpit_library(voice_vehicle_client agent/vehicle/gateway_vehicle_status_client.cc)
target_link_libraries(voice_vehicle_client PUBLIC contracts voice)

add_cockpit_library(voice_hmi agent/hmi/local_hmi_command_provider.cc)
target_link_libraries(voice_hmi PUBLIC contracts voice)

add_cockpit_library(voice_grpc agent/grpc/voice_grpc_service.cc)
target_link_libraries(voice_grpc PUBLIC contracts voice_interaction)
target_link_libraries(voice_grpc PRIVATE logging time_util)

add_library(agent_runtime STATIC agent/agent_runtime.cc)
target_include_directories(agent_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_runtime PUBLIC
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

add_library(hmi_runtime STATIC hmi/hmi_runtime.cc)
target_include_directories(hmi_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(hmi_runtime PUBLIC config logging)

add_library(carupload_runtime STATIC carupload/carupload_runtime.cc)
target_include_directories(carupload_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(carupload_runtime PUBLIC config logging vehicle)

add_cockpit_library(recording_service recording/recording_service.cc)
target_link_libraries(recording_service PUBLIC contracts logging recording)

add_library(recording_runtime STATIC
    recording/recording_grpc_service.cc
    recording/recording_runtime.cc
    recording/vehicle_state_subscriber.cc
)
target_include_directories(recording_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(recording_runtime PUBLIC
    build_info
    config
    contracts
    logging
    recording_service
    time_util
)

add_library(upgrader_runtime STATIC
    upgrader/upgrade_transaction.cc
    upgrader/upgrader_runtime.cc
)
target_include_directories(upgrader_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(upgrader_runtime PUBLIC config logging)
