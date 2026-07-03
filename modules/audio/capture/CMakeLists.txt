find_package(Threads REQUIRED)

add_cockpit_library(audio_capture audio_capture_stream.cc)
target_link_libraries(audio_capture PUBLIC audio_frames Threads::Threads)
