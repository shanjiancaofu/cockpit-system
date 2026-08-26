add_library(hawkeye STATIC
    camera_calibration.cc
    camera_calibration_loader.cc
    camera_info.cc
)
target_include_directories(hawkeye PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(hawkeye PRIVATE yaml-cpp)
