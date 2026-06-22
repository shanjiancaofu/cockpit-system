if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(alsa)
endif()
add_subdirectory(socketcan)
