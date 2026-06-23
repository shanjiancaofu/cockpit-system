cmake_minimum_required(VERSION 3.16)

project(cockpit-system
    VERSION 0.1.0
    DESCRIPTION "Jetson smart cockpit prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_COCKPIT_UI "Build Qt cockpit UI" OFF)
option(BUILD_WEB_DASHBOARD "Build web dashboard" OFF)
option(BUILD_TESTS "Build unit tests" ON)

include(cmake/AddCockpitLibrary.cmake)

add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(drivers)
add_subdirectory(proto)
add_subdirectory(services/vehicle-data-service)
add_subdirectory(services/cockpit-gateway-service)
add_subdirectory(services/cloud-uplink-service)
add_subdirectory(services/audio-service)
add_subdirectory(services/camera-service)
add_subdirectory(services/voice-interaction-service)
add_subdirectory(tools/can-simulator)
add_subdirectory(tools/audio-probe)
add_subdirectory(tools/camera-ctl)
add_subdirectory(tools/camera-probe)
add_subdirectory(tools/camera-preview-probe)
add_subdirectory(tools/voice-ctl)
add_subdirectory(tools/topic)

if(BUILD_COCKPIT_UI)
    add_subdirectory(apps/cockpit-ui)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
