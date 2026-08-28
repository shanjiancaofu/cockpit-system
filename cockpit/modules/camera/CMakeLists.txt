find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS imgproc)
pkg_check_modules(GSTREAMER REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

add_library(camera_isp STATIC isp/software_isp.cc)
target_include_directories(camera_isp PUBLIC ${PROJECT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camera_isp PUBLIC camera_frames ${OpenCV_LIBS})

add_library(camera_frames STATIC frames/camera_frame.cc frames/latest_frame_buffer.cc)

add_library(camera_capture INTERFACE)

add_library(v4l2_camera_source STATIC capture/v4l2_preview_source.cc)
target_link_libraries(v4l2_camera_source PUBLIC camera_capture camera_isp v4l2_camera time_util Threads::Threads)
target_include_directories(v4l2_camera_source PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(camera_capture INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(camera_capture INTERFACE camera_frames)

add_library(synthetic_camera STATIC capture/synthetic_preview_source.cc)
target_link_libraries(synthetic_camera PUBLIC camera_capture time_util Threads::Threads)

add_library(gstreamer_camera STATIC capture/gstreamer_preview_pipeline.cc)
target_include_directories(gstreamer_camera PUBLIC ${GSTREAMER_INCLUDE_DIRS})
target_link_directories(gstreamer_camera PUBLIC ${GSTREAMER_LIBRARY_DIRS})
target_link_libraries(gstreamer_camera PUBLIC camera_capture time_util ${GSTREAMER_LIBRARIES})
target_compile_options(gstreamer_camera PRIVATE ${GSTREAMER_CFLAGS_OTHER})

add_library(camera_shm STATIC shared_memory/shared_frame_buffer.cc)
target_link_libraries(camera_shm PUBLIC camera_frames ipc Threads::Threads)

add_library(camera_photo STATIC photo/jpeg_encoder.cc)
target_link_libraries(camera_photo PUBLIC camera_frames)
target_include_directories(camera_photo PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_directories(camera_photo PRIVATE ${GSTREAMER_LIBRARY_DIRS})
target_link_libraries(camera_photo PRIVATE ${GSTREAMER_LIBRARIES})
target_compile_options(camera_photo PRIVATE ${GSTREAMER_CFLAGS_OTHER})

add_library(camera INTERFACE)
target_link_libraries(camera INTERFACE camera_capture camera_frames camera_photo)
