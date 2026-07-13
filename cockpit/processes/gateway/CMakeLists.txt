add_executable(cockpit-gateway-service main.cc)
target_link_libraries(cockpit-gateway-service PRIVATE runtime transfer_runtime)
