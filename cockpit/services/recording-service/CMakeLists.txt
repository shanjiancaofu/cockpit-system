add_subdirectory(client)

add_cockpit_library(recording_service recording_service.cc)
target_link_libraries(recording_service PUBLIC contracts recording)

add_executable(recording-service
    main.cc
    recording_grpc_service.cc
    vehicle_state_subscriber.cc
)
target_link_libraries(recording-service PRIVATE
    contracts
    logging
    recording_service
    runtime
    utils
)
