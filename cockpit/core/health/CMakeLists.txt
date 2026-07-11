add_library(service_health
    service_health.cc
)

target_include_directories(service_health PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(service_health PUBLIC contracts)
