add_subdirectory(foundation)
add_subdirectory(proto)
add_library(camera_calibration_contracts STATIC
    contracts/camera/camera_calibration.cc
    contracts/camera/camera_calibration_loader.cc
    contracts/camera/camera_info.cc
)
target_link_libraries(camera_calibration_contracts PUBLIC yaml-cpp)
