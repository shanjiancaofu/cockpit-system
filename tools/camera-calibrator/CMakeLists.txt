find_package(OpenCV REQUIRED COMPONENTS calib3d imgcodecs imgproc)

add_executable(camera-calibrator camera_calibrator.cc)
target_include_directories(camera-calibrator PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camera-calibrator PRIVATE gstreamer_camera camera_frames ${OpenCV_LIBS})
