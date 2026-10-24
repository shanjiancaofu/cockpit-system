include_guard(GLOBAL)

option(COCKPIT_ENABLE_NATIVE_ARGUS "Build the isolated native Argus runtime when SDKs are available" ON)
set(COCKPIT_JETSON_MULTIMEDIA_ROOT "/usr/src/jetson_multimedia_api" CACHE PATH "Jetson Multimedia SDK root")
if(COCKPIT_ENABLE_NATIVE_ARGUS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
        set(COCKPIT_CAMERA_HAS_NATIVE_ARGUS TRUE)
    endif()
endif()
