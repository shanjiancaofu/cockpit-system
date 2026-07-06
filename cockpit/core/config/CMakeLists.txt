add_cockpit_library(config
    system_config.cc
)

find_package(yaml-cpp REQUIRED)
target_link_libraries(config PUBLIC yaml-cpp)
