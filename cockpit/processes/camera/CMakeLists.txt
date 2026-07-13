add_executable(camera-service main.cc)
target_link_libraries(camera-service PRIVATE camera_runtime runtime)
