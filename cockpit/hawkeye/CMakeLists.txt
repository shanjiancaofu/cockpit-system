add_library(hawkeye STATIC lidar_camera_projection.cc)
target_include_directories(hawkeye PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(hawkeye PUBLIC camera_calibration_contracts)
