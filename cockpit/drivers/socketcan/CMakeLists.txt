if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(socketcan STATIC
        socket_can.cc
    )

    target_link_libraries(socketcan PUBLIC can)
endif()
