add_executable(recording-ctl main.cc recording_control_client.cc)
target_link_libraries(recording-ctl PRIVATE contracts runtime)
