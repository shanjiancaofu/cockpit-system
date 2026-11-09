add_library(transfer_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/camera_keyframe_client.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/gateway_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/transfer_runtime.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/vehicle_state_client.cc
)
target_include_directories(transfer_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_runtime PUBLIC config contracts logging time_util)

if(GSTREAMER_RTSP_FOUND)
    add_library(rtsp_server STATIC ${PROJECT_SOURCE_DIR}/cockpit/transfer/rtsp/rtsp_server.cc)
    target_link_libraries(rtsp_server PUBLIC PkgConfig::GSTREAMER_RTSP PkgConfig::GSTREAMER)
    set(COCKPIT_LOCAL_GSTREAMER_ROOT
        "${CMAKE_SOURCE_DIR}/../../../_output/deps/gstreamer/usr")
    if(EXISTS "${COCKPIT_LOCAL_GSTREAMER_ROOT}/include/gstreamer-1.0")
        target_include_directories(rtsp_server SYSTEM PRIVATE
            "${COCKPIT_LOCAL_GSTREAMER_ROOT}/include/gstreamer-1.0")
        target_link_directories(rtsp_server PUBLIC
            "${COCKPIT_LOCAL_GSTREAMER_ROOT}/lib/aarch64-linux-gnu")
    endif()
else()
    add_library(rtsp_server STATIC ${PROJECT_SOURCE_DIR}/cockpit/transfer/rtsp/rtsp_server_stub.cc)
    message(STATUS "GStreamer RTSP Server development package unavailable: RTSP stub")
endif()
target_include_directories(rtsp_server PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_runtime PUBLIC rtsp_server)

add_library(transfer_video STATIC ../video/h264_receiver.cc)
target_include_directories(transfer_video PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_video PUBLIC Threads::Threads)
target_link_libraries(rtsp_server PUBLIC transfer_video)
if(COCKPIT_ENABLE_WEBRTC)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../webrtc/web/index.html" COCKPIT_WEBRTC_PAGE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../webrtc/web/index.html")
    configure_file(../webrtc/web_page.h.in webrtc_web_page.h @ONLY)
    add_library(webrtc_server STATIC ../webrtc/webrtc_server.cc ../webrtc/h264_track.cc)
    target_include_directories(webrtc_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(webrtc_server PUBLIC LibDataChannel::LibDataChannel PkgConfig::WEBRTC_HTTP)
else()
    add_library(webrtc_server STATIC ../webrtc/webrtc_server_stub.cc)
endif()
target_include_directories(webrtc_server PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(webrtc_server PUBLIC transfer_video)
target_link_libraries(transfer_runtime PUBLIC webrtc_server)
