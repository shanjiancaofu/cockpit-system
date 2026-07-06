add_subdirectory(preview)
add_subdirectory(control)
add_subdirectory(grpc)

add_library(camera_service INTERFACE)
target_link_libraries(camera_service INTERFACE camera_control)

add_executable(camera-service main.cc)
target_link_libraries(camera-service PRIVATE camera_control camera_grpc camera_shm logging runtime)
