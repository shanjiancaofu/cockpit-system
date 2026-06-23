add_cockpit_library(camera_service
    camera_service.cc
)
target_link_libraries(camera_service PUBLIC camera v4l2_camera)

add_executable(camera-service
    camera_grpc_service.cc
    main.cc
)
target_link_libraries(camera-service
    PRIVATE camera_service contracts logging runtime
)
