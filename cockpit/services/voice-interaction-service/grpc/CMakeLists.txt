add_cockpit_library(voice_grpc
    voice_grpc_service.cc
)
target_link_libraries(voice_grpc PUBLIC contracts voice_interaction)
target_link_libraries(voice_grpc PRIVATE logging utils)
