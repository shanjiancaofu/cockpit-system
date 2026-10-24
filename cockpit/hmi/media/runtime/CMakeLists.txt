add_library(media_service STATIC ${PROJECT_SOURCE_DIR}/cockpit/hmi/media/runtime/media_service.cc)
target_include_directories(media_service PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(media_service PUBLIC media)

add_library(media_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/hmi/media/runtime/media_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/hmi/media/runtime/media_runtime.cc
)
target_include_directories(media_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(media_runtime PUBLIC config contracts logging media media_service time_util)

