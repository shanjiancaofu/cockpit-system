if(TARGET gstreamer_camera)
    add_library(camera_preview_probe_commands STATIC camera_preview_probe.cc)
    target_include_directories(camera_preview_probe_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(camera_preview_probe_commands PRIVATE gstreamer_camera logging runtime)

    add_executable(camera-preview-probe main.cc)
    target_link_libraries(camera-preview-probe PRIVATE camera_preview_probe_commands)
endif()
