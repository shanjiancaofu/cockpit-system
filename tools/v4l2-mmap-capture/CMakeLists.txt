add_executable(v4l2-mmap-capture v4l2_mmap_capture.cc)
target_link_libraries(v4l2-mmap-capture PRIVATE cockpit_project_options v4l2_camera)
