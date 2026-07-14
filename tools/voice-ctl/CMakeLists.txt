add_executable(voice-ctl
    main.cc
    voice_ctl.cc
    voice_control_client.cc
)
target_include_directories(voice-ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(voice-ctl PRIVATE contracts diagnostic_cli logging runtime)
