add_executable(cockpit-gateway-service
    main.cc
    vehicle_state_client.cc
)
target_link_libraries(cockpit-gateway-service PRIVATE contracts logging runtime vehicle)
