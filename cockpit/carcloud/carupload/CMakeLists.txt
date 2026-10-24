add_library(carupload_runtime STATIC ${PROJECT_SOURCE_DIR}/cockpit/carcloud/carupload/carupload_runtime.cc)
target_include_directories(carupload_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(carupload_runtime PUBLIC config logging vehicle)

