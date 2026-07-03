find_package(Threads REQUIRED)

add_cockpit_library(camera_shm shared_frame_buffer.cc)
target_link_libraries(camera_shm PUBLIC camera_frames ipc Threads::Threads)
