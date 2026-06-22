add_executable(voice-ctl
    main.cc
    voice_control_client.cc
)
target_link_libraries(voice-ctl PRIVATE contracts logging runtime)
