add_subdirectory(preview)
add_subdirectory(control)
add_subdirectory(photo)
add_subdirectory(grpc)

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
