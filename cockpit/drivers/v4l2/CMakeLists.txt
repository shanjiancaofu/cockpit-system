if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(v4l2_camera STATIC
        v4l2_camera.cc
    )
endif()
