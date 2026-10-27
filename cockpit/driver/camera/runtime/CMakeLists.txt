add_library(camera_preview STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/preview/camera_preview_module.cc)
target_link_libraries(camera_preview PUBLIC camera_capture runtime software_isp_camera_source)

add_library(camera_control STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/control/camera_service.cc)
target_link_libraries(camera_control PUBLIC base camera_frames camera_preview native_argus_preview event camera_calibration_contracts runtime v4l2_camera)
target_link_libraries(camera_control PRIVATE gstreamer_camera json_util)

add_library(camera_photo_service STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/photo/camera_photo_service.cc)
target_link_libraries(camera_photo_service PUBLIC camera_photo camera_shm time_util)

add_library(camera_grpc STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/grpc/camera_grpc_service.cc)
target_link_libraries(camera_grpc PUBLIC
    camera_control
    camera_photo_service
    contracts
    recording_event_client
)
target_link_libraries(camera_grpc PRIVATE json_util logging time_util)

add_library(camera_recording_bridge STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/recording_bridge.cc)
target_link_libraries(camera_recording_bridge PUBLIC event)

add_library(camera_service INTERFACE)
target_link_libraries(camera_service INTERFACE camera_control camera_recording_bridge)

add_library(camera_runtime STATIC ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/camera_runtime.cc)
target_include_directories(camera_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(camera_runtime PUBLIC
    camera_control
    camera_grpc
    camera_photo_service
    camera_recording_bridge
    camera_calibration_contracts
    camera_shm
    config
    logging
    recording_event_client
    synthetic_camera
)

