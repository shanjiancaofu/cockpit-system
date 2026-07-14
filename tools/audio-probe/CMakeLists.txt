if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(audio-probe
        main.cc
        audio_control_client.cc
        audio_probe.cc
    )
    target_include_directories(audio-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(audio-probe PRIVATE
        alsa_audio audio contracts diagnostic_cli logging runtime
    )
endif()
