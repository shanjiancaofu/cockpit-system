if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(socketcan STATIC
        socket_can.cc
    )
endif()
