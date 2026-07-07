add_subdirectory(frames)
add_subdirectory(capture)
add_subdirectory(shared_memory)
add_subdirectory(photo)

add_library(camera INTERFACE)
target_link_libraries(camera INTERFACE camera_capture camera_frames camera_photo)
