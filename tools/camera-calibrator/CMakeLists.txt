find_package(OpenCV QUIET COMPONENTS calib3d highgui imgcodecs imgproc)

if(OpenCV_FOUND AND TARGET gstreamer_camera)
  add_executable(camera-calibrator
    camera_calibrator_main.cc
    camera_calibrator_options.cc
    camera_calibrator_core.cc
  )
  target_include_directories(camera-calibrator PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(camera-calibrator PRIVATE gstreamer_camera camera_frames ${OpenCV_LIBS})
endif()
