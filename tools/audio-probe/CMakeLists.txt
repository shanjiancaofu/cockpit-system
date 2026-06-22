if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(audio-probe
        audio_control_client.cc
        main.cc
    )
    target_link_libraries(audio-probe PRIVATE alsa_audio audio contracts logging runtime)
endif()
