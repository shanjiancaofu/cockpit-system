# Added separately from production camera modules; absent SDKs do not break other tools.
set(COCKPIT_JETSON_MULTIMEDIA_ROOT "/usr/src/jetson_multimedia_api" CACHE PATH "Jetson Multimedia SDK root")
find_path(COCKPIT_ARGUS_INCLUDE_DIR Argus/Argus.h
    HINTS "${COCKPIT_JETSON_MULTIMEDIA_ROOT}/argus/include")
find_path(COCKPIT_NVBUFSURFACE_INCLUDE_DIR nvbufsurface.h
    HINTS "${COCKPIT_JETSON_MULTIMEDIA_ROOT}/include")
find_library(COCKPIT_ARGUS_LIBRARY nvargus_socketclient
    HINTS /usr/lib/aarch64-linux-gnu/nvidia /usr/lib/aarch64-linux-gnu/tegra)
find_library(COCKPIT_NVBUFSURFACE_LIBRARY nvbufsurface
    HINTS /usr/lib/aarch64-linux-gnu/nvidia /usr/lib/aarch64-linux-gnu/tegra)
find_library(COCKPIT_EGL_LIBRARY EGL)
find_path(COCKPIT_EGL_INCLUDE_DIR EGL/egl.h)
if(COCKPIT_ARGUS_INCLUDE_DIR AND COCKPIT_NVBUFSURFACE_INCLUDE_DIR AND
   COCKPIT_ARGUS_LIBRARY AND COCKPIT_NVBUFSURFACE_LIBRARY AND
   COCKPIT_EGL_LIBRARY AND COCKPIT_EGL_INCLUDE_DIR)
    add_executable(libargus-capture-probe main.cc)
    target_include_directories(libargus-capture-probe SYSTEM PRIVATE
        "${COCKPIT_ARGUS_INCLUDE_DIR}" "${COCKPIT_NVBUFSURFACE_INCLUDE_DIR}" "${COCKPIT_EGL_INCLUDE_DIR}")
    target_link_libraries(libargus-capture-probe PRIVATE camera_diagnostics
        "${COCKPIT_ARGUS_LIBRARY}" "${COCKPIT_NVBUFSURFACE_LIBRARY}" "${COCKPIT_EGL_LIBRARY}")
else()
    message(STATUS "libargus-capture-probe disabled: requires Jetson libargus, NvBufSurface and EGL SDKs")
endif()
