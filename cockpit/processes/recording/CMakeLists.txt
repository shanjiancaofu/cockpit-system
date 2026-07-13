add_executable(recording-service main.cc)
target_link_libraries(recording-service PRIVATE recording_runtime runtime)
