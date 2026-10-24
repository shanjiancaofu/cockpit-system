add_executable(native-frame-broker-probe main.cc)
target_link_libraries(native-frame-broker-probe PRIVATE native_camera_capture camera_frame_broker
    camera_vic_transform camera_diagnostics)
