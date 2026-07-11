if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(camera_probe_commands STATIC camera_probe.cc)
    target_include_directories(camera_probe_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(camera_probe_commands PRIVATE logging runtime v4l2_camera)

    add_executable(camera-probe main.cc)
    target_link_libraries(camera-probe PRIVATE camera_probe_commands)
endif()
