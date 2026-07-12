add_cockpit_library(camera_preview preview/camera_preview_module.cc)
target_link_libraries(camera_preview PUBLIC camera_capture runtime)

add_cockpit_library(camera_control control/camera_service.cc)
target_link_libraries(camera_control PUBLIC base camera_frames camera_preview event runtime v4l2_camera)
target_link_libraries(camera_control PRIVATE json_util)
if(TARGET gstreamer_camera)
    target_compile_definitions(camera_control PRIVATE COCKPIT_HAS_GSTREAMER_CAMERA=1)
    target_link_libraries(camera_control PRIVATE gstreamer_camera)
endif()

add_cockpit_library(camera_photo_service photo/camera_photo_service.cc)
target_link_libraries(camera_photo_service PUBLIC camera_photo camera_shm time_util)

add_cockpit_library(camera_grpc grpc/camera_grpc_service.cc)
target_link_libraries(camera_grpc PUBLIC camera_control camera_photo_service contracts recording_event_client)
target_link_libraries(camera_grpc PRIVATE json_util logging time_util)

add_cockpit_library(camera_recording_bridge recording_bridge.cc)
target_link_libraries(camera_recording_bridge PUBLIC event)

add_library(camera_service INTERFACE)
target_link_libraries(camera_service INTERFACE camera_control camera_recording_bridge)

add_executable(camera-service main.cc)
target_link_libraries(camera-service PRIVATE
    camera_control
    camera_grpc
    camera_photo_service
    camera_recording_bridge
    camera_shm
    synthetic_camera
    logging
    recording_event_client
    runtime
)
