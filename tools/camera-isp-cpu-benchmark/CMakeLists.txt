if(TARGET camera_isp)
  add_executable(camera-isp-cpu-benchmark camera_isp_cpu_benchmark.cc)
  target_link_libraries(camera-isp-cpu-benchmark PRIVATE camera_isp)
endif()
