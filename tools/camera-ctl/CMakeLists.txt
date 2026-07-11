add_library(camera_ctl_commands STATIC
    camera_control_client.cc
    camera_ctl.cc
)
target_include_directories(camera_ctl_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(camera_ctl_commands PRIVATE contracts diagnostic_cli logging runtime)

add_executable(camera-ctl main.cc)
target_link_libraries(camera-ctl PRIVATE camera_ctl_commands)
