if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_cockpit_library(v4l2_camera
        v4l2_camera.cc
    )
endif()
