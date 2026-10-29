include_guard(GLOBAL)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Protobuf 3.12.4 EXACT REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavutil libswscale)
pkg_check_modules(X264 QUIET IMPORTED_TARGET x264)
set(COCKPIT_LOCAL_GSTREAMER_PC
    "${CMAKE_SOURCE_DIR}/../../../_output/deps/gstreamer/usr/lib/aarch64-linux-gnu/pkgconfig")
if(EXISTS "${COCKPIT_LOCAL_GSTREAMER_PC}/gstreamer-rtsp-server-1.0.pc")
    set(ENV{PKG_CONFIG_PATH} "${COCKPIT_LOCAL_GSTREAMER_PC}:$ENV{PKG_CONFIG_PATH}")
endif()
pkg_check_modules(GSTREAMER_RTSP QUIET IMPORTED_TARGET gstreamer-rtsp-server-1.0)
set(COCKPIT_HAS_H264_ENCODER FALSE)
if(FFMPEG_FOUND AND X264_FOUND)
    set(COCKPIT_HAS_H264_ENCODER TRUE)
endif()
find_program(COCKPIT_GRPC_CPP_PLUGIN_EXECUTABLE
    NAMES grpc_cpp_plugin
    REQUIRED
)
