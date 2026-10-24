if(TARGET camera_calibrator)
  add_executable(camera-calibrator camera_calibrator_main.cc)
  target_link_libraries(camera-calibrator PRIVATE camera_calibrator)
endif()
