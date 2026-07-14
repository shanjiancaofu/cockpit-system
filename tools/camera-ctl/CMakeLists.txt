add_executable(camera-ctl
    main.cc
    camera_control_client.cc
    camera_ctl.cc
)
target_include_directories(camera-ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(camera-ctl PRIVATE contracts diagnostic_cli logging runtime)
