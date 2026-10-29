add_library(transfer_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/gateway_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/transfer_runtime.cc
    ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/vehicle_state_client.cc
)
target_include_directories(transfer_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(transfer_runtime PUBLIC config contracts logging time_util)

if(GSTREAMER_RTSP_FOUND)
    add_library(rtsp_server STATIC ${PROJECT_SOURCE_DIR}/cockpit/transfer/rtsp/rtsp_server.cc)
    target_link_libraries(rtsp_server PUBLIC PkgConfig::GSTREAMER_RTSP)
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

