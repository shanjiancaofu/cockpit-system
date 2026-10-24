add_library(hmi_runtime STATIC ${PROJECT_SOURCE_DIR}/cockpit/hmi/runtime/hmi_runtime.cc)
target_include_directories(hmi_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(hmi_runtime PUBLIC config logging)

