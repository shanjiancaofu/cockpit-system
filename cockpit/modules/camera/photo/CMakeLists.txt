add_cockpit_library(camera_photo jpeg_encoder.cc)
target_link_libraries(camera_photo PUBLIC camera_frames)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER_PHOTO QUIET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
endif()

if(GSTREAMER_PHOTO_FOUND)
    target_compile_definitions(camera_photo PRIVATE COCKPIT_HAS_GSTREAMER_PHOTO=1)
    target_include_directories(camera_photo PRIVATE ${GSTREAMER_PHOTO_INCLUDE_DIRS})
    target_link_directories(camera_photo PRIVATE ${GSTREAMER_PHOTO_LIBRARY_DIRS})
    target_link_libraries(camera_photo PRIVATE ${GSTREAMER_PHOTO_LIBRARIES})
    target_compile_options(camera_photo PRIVATE ${GSTREAMER_PHOTO_CFLAGS_OTHER})
endif()
