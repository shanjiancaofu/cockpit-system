if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_cockpit_library(socketcan
        socket_can.cc
    )

    target_link_libraries(socketcan PUBLIC can)
endif()
