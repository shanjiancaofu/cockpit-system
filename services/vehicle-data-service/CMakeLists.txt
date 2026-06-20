add_executable(vehicle-data-service
    main.cc
    vehicle_data_service.cc
)
target_link_libraries(vehicle-data-service PRIVATE logging runtime socketcan vehicle)
