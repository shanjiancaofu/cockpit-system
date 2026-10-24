add_library(audio_capture_module STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/capture/audio_capture_module.cc)
target_link_libraries(audio_capture_module PUBLIC audio_capture runtime)

add_library(audio_stream_publisher STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/transport/audio_stream_publisher.cc)
target_link_libraries(audio_stream_publisher PUBLIC audio_stream_protocol Threads::Threads)

add_library(audio_device_playback STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/playback/audio_playback.cc)
target_link_libraries(audio_device_playback PUBLIC audio_playback)
target_link_libraries(audio_device_playback PRIVATE logging)

add_library(audio_capture_controller STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/capture/audio_capture_controller.cc)
target_link_libraries(audio_capture_controller PUBLIC
    alsa_audio audio_analysis audio_capture_module audio_stream_publisher config
)

add_library(audio_grpc STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/grpc/audio_grpc_server.cc)
target_link_libraries(audio_grpc PUBLIC audio_capture_controller audio_device_playback contracts)
target_link_libraries(audio_grpc PRIVATE logging time_util)

add_library(audio_runtime STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/audio_runtime.cc)
target_include_directories(audio_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(audio_runtime PUBLIC
    alsa_audio
    audio_grpc
    audio_capture_controller
    audio_device_playback
    config
    logging
)

