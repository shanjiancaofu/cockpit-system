if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(audio_probe_commands STATIC
        audio_control_client.cc
        audio_probe.cc
    )
    target_include_directories(audio_probe_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(audio_probe_commands PRIVATE
        alsa_audio audio contracts diagnostic_cli logging runtime
    )

    add_executable(audio-probe main.cc)
    target_link_libraries(audio-probe PRIVATE audio_probe_commands)
endif()
