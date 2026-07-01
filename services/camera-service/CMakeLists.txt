add_cockpit_library(camera_service
    camera_preview_module.cc
    camera_service.cc
)
target_link_libraries(camera_service PUBLIC camera runtime v4l2_camera)
if(TARGET gstreamer_camera)
    target_compile_definitions(camera_service PRIVATE COCKPIT_HAS_GSTREAMER_CAMERA=1)
    target_link_libraries(camera_service PRIVATE gstreamer_camera)
endif()

add_executable(camera-service
    camera_grpc_service.cc
    main.cc
)
target_link_libraries(camera-service
    PRIVATE camera_service camera_shm contracts logging runtime
)
