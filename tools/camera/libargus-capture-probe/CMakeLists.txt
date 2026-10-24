# The runtime owns SDK detection and all platform dependencies.
add_executable(libargus-capture-probe main.cc)
target_link_libraries(libargus-capture-probe PRIVATE camera_diagnostics native_camera_capture)
