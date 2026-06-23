add_executable(camera-ctl
    camera_control_client.cc
    main.cc
)
target_link_libraries(camera-ctl PRIVATE contracts logging runtime)
