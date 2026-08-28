if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(v4l2_camera STATIC
        v4l2_camera.cc
        v4l2_mmap_capture.cc
    )
    target_include_directories(v4l2_camera PUBLIC ${PROJECT_SOURCE_DIR})
endif()
