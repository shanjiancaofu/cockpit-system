add_cockpit_library(camera
    camera_frame.cc
    latest_frame_buffer.cc
)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
endif()

if(GSTREAMER_FOUND)
    add_cockpit_library(gstreamer_camera
        gstreamer_preview_pipeline.cc
    )
    target_include_directories(gstreamer_camera PUBLIC ${GSTREAMER_INCLUDE_DIRS})
    target_link_directories(gstreamer_camera PUBLIC ${GSTREAMER_LIBRARY_DIRS})
    target_link_libraries(gstreamer_camera PUBLIC camera utils ${GSTREAMER_LIBRARIES})
    target_compile_options(gstreamer_camera PRIVATE ${GSTREAMER_CFLAGS_OTHER})
endif()
