add_executable(cloud-uplink-service main.cc)
target_link_libraries(cloud-uplink-service PRIVATE carupload_runtime runtime)
