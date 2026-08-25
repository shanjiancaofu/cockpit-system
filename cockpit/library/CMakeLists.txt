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
target_link_libraries(vehicle_runtime PUBLIC can config contracts logging vehicle)

add_library(audio_capture_module STATIC driver/audio/capture/audio_capture_module.cc)
target_link_libraries(audio_capture_module PUBLIC audio_capture runtime)

add_library(audio_stream_publisher STATIC driver/audio/transport/audio_stream_publisher.cc)
target_link_libraries(audio_stream_publisher PUBLIC audio_stream_protocol Threads::Threads)

add_library(audio_device_playback STATIC driver/audio/playback/audio_playback.cc)
target_link_libraries(audio_device_playback PUBLIC audio_playback)
target_link_libraries(audio_device_playback PRIVATE logging)

add_library(audio_capture_controller STATIC driver/audio/capture/audio_capture_controller.cc)
target_link_libraries(audio_capture_controller PUBLIC
    alsa_audio audio_analysis audio_capture_module audio_stream_publisher config
)

add_library(audio_grpc STATIC driver/audio/grpc/audio_grpc_server.cc)
target_link_libraries(audio_grpc PUBLIC audio_capture_controller audio_device_playback contracts)
target_link_libraries(audio_grpc PRIVATE logging time_util)

add_library(audio_runtime STATIC driver/audio/audio_runtime.cc)
target_include_directories(audio_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(audio_runtime PUBLIC
    alsa_audio
    audio_grpc
    audio_capture_controller
    audio_device_playback
    config
    logging
)

add_library(camera_preview STATIC driver/camera/preview/camera_preview_module.cc)
target_link_libraries(camera_preview PUBLIC camera_capture runtime)

add_library(camera_control STATIC driver/camera/control/camera_service.cc)
target_link_libraries(camera_control PUBLIC base camera_frames camera_preview event runtime v4l2_camera)
target_link_libraries(camera_control PRIVATE gstreamer_camera json_util)

add_library(camera_photo_service STATIC driver/camera/photo/camera_photo_service.cc)
target_link_libraries(camera_photo_service PUBLIC camera_photo camera_shm time_util)

add_library(camera_grpc STATIC driver/camera/grpc/camera_grpc_service.cc)
target_link_libraries(camera_grpc PUBLIC
    camera_control
    camera_photo_service
    contracts
    recording_event_client
)
target_link_libraries(camera_grpc PRIVATE json_util logging time_util)

add_library(camera_recording_bridge STATIC driver/camera/recording_bridge.cc)
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

add_library(hmi_runtime STATIC hmi/hmi_runtime.cc)
target_include_directories(hmi_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(hmi_runtime PUBLIC config logging)

add_library(media_service STATIC media/media_service.cc)
target_include_directories(media_service PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(media_service PUBLIC media)

add_library(media_runtime STATIC
    media/media_grpc_service.cc
    media/media_runtime.cc
)
target_include_directories(media_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(media_runtime PUBLIC config contracts logging media media_service time_util)

add_library(bridge_runtime STATIC
    bridge/bridge_grpc_service.cc
    bridge/bridge_runtime.cc
)
target_include_directories(bridge_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(bridge_runtime PUBLIC config contracts logging bridge time_util)

add_library(carupload_runtime STATIC carupload/carupload_runtime.cc)
target_include_directories(carupload_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(carupload_runtime PUBLIC config logging vehicle)

add_library(recording_service STATIC recording/recording_service.cc)
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

add_library(sentinel_runtime STATIC
    sentinel/chassis_event_client.cc
    sentinel/sentinel_actions.cc
    sentinel/sentinel_grpc_service.cc
    sentinel/sentinel_runtime.cc
)
target_include_directories(sentinel_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(sentinel_runtime PUBLIC
    config contracts json_util logging sentinel time_util
)

add_library(upgrader_runtime STATIC
    upgrader/upgrade_transaction.cc
    upgrader/upgrader_runtime.cc
)
target_include_directories(upgrader_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(upgrader_runtime PUBLIC config logging)
