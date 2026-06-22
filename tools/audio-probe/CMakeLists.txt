if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(audio-probe main.cc)
    target_link_libraries(audio-probe PRIVATE alsa_audio audio logging runtime)
endif()
