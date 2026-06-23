if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(camera-probe
        main.cc
    )
    target_link_libraries(camera-probe PRIVATE logging runtime v4l2_camera)
endif()
