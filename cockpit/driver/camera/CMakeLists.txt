find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS imgproc)
pkg_check_modules(GSTREAMER REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

add_library(camera_frames STATIC frames/camera_frame.cc frames/latest_frame_buffer.cc frames/native_frame.cc)
add_library(camera_frame_broker STATIC frames/frame_broker.cc)
target_link_libraries(camera_frame_broker PUBLIC camera_frames Threads::Threads)

add_library(camera_isp STATIC
    isp/raw10_unpack.cc
    isp/software_isp.cc
)
if(COCKPIT_TARGET_ARCH STREQUAL "arm64")
    target_sources(camera_isp PRIVATE isp/raw10_unpack_neon.cc)
    target_compile_definitions(camera_isp PRIVATE COCKPIT_CAMERA_HAS_NEON=1)
endif()
target_include_directories(camera_isp PUBLIC ${PROJECT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camera_isp PUBLIC camera_frames ${OpenCV_LIBS})

add_library(camera_capture INTERFACE)

include(${PROJECT_SOURCE_DIR}/cmake/jetson_camera.cmake)
if(COCKPIT_CAMERA_HAS_NATIVE_ARGUS)
    add_library(native_camera_capture STATIC capture/native_argus_capture.cc)
    target_include_directories(native_camera_capture SYSTEM PRIVATE
        "${COCKPIT_ARGUS_INCLUDE_DIR}" "${COCKPIT_NVBUFSURFACE_INCLUDE_DIR}" "${COCKPIT_EGL_INCLUDE_DIR}")
    target_link_libraries(native_camera_capture PRIVATE
        "${COCKPIT_ARGUS_LIBRARY}" "${COCKPIT_NVBUFSURFACE_LIBRARY}" "${COCKPIT_EGL_LIBRARY}")
else()
    add_library(native_camera_capture STATIC capture/native_argus_capture_stub.cc)
    message(STATUS "Native Argus SDK unavailable/disabled: building explicit unsupported runtime")
endif()
target_link_libraries(native_camera_capture PUBLIC camera_frames time_util Threads::Threads)

find_library(COCKPIT_VIC_LIBRARY nvbufsurftransform
    HINTS /usr/lib/aarch64-linux-gnu/nvidia /usr/lib/aarch64-linux-gnu/tegra)
if(COCKPIT_CAMERA_HAS_NATIVE_ARGUS AND COCKPIT_VIC_LIBRARY)
    add_library(camera_vic_transform STATIC transform/vic_transform.cc transform/cpu_video_frame.cc)
    target_include_directories(camera_vic_transform SYSTEM PRIVATE "${COCKPIT_NVBUFSURFACE_INCLUDE_DIR}")
    target_link_libraries(camera_vic_transform PRIVATE "${COCKPIT_NVBUFSURFACE_LIBRARY}" "${COCKPIT_VIC_LIBRARY}")
    target_compile_definitions(camera_vic_transform PUBLIC COCKPIT_CAMERA_HAS_VIC=1)
else()
    add_library(camera_vic_transform STATIC transform/vic_transform_stub.cc transform/cpu_video_frame.cc)
endif()
target_link_libraries(camera_vic_transform PUBLIC camera_frame_broker Threads::Threads)

add_library(software_isp_camera_source STATIC capture/software_isp_preview_source.cc)
target_link_libraries(software_isp_camera_source PUBLIC
    camera_capture camera_isp v4l2_camera time_util Threads::Threads
)
target_include_directories(software_isp_camera_source PUBLIC ${PROJECT_SOURCE_DIR})
add_library(synthetic_camera STATIC capture/synthetic_preview_source.cc)
target_link_libraries(synthetic_camera PUBLIC camera_capture time_util Threads::Threads)

add_library(gstreamer_camera STATIC
    capture/gstreamer_preview_pipeline.cc
    capture/gstreamer_timestamp_mapper.cc
)
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
