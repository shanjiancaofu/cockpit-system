add_executable(recording-ctl main.cc recording_ctl.cc recording_control_client.cc)
target_include_directories(recording-ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(recording-ctl PRIVATE contracts diagnostic_cli runtime time_util)
