add_cockpit_library(recording_service recording_service.cc)
target_link_libraries(recording_service PUBLIC contracts logging recording)

add_executable(recording-service
    main.cc
    recording_grpc_service.cc
    vehicle_state_subscriber.cc
)
target_link_libraries(recording-service PRIVATE
    build_info
    contracts
    logging
    recording_service
    runtime
    utils
)
