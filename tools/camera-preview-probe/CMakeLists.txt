if(TARGET gstreamer_camera)
    add_executable(camera-preview-probe
        main.cc
    )
    target_link_libraries(camera-preview-probe PRIVATE gstreamer_camera logging runtime)
endif()
