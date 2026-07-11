add_library(voice_ctl_commands STATIC
    voice_ctl.cc
    voice_control_client.cc
)
target_include_directories(voice_ctl_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(voice_ctl_commands PRIVATE contracts diagnostic_cli logging runtime)

add_executable(voice-ctl main.cc)
target_link_libraries(voice-ctl PRIVATE voice_ctl_commands)
