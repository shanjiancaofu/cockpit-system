add_library(bridge STATIC
    fake_bridge_provider.cc
    bridge_service.cc
)
target_include_directories(bridge PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(bridge PUBLIC time_util)
