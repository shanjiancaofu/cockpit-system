if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

add_library(camera_diagnostics STATIC
    camera-diag/device_inspector.cc
    camera-diag/sensor_inspector.cc
    camera-diag/control_inspector.cc
    camera-diag/report_writer.cc
    camera-diag/capture_stress.cc
    camera-diag/runner.cc
)
target_link_libraries(camera_diagnostics PUBLIC v4l2_camera json_util Threads::Threads)
add_executable(camera-diag camera-diag/main.cc)
target_link_libraries(camera-diag PRIVATE camera_diagnostics)
add_executable(camera-stress camera-stress/main.cc)
target_link_libraries(camera-stress PRIVATE camera_diagnostics)

add_subdirectory(libargus-capture-probe)
