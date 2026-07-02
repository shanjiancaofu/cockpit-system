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
option(BUILD_WHISPER_CPP_ASR "Build the optional whisper.cpp ASR provider" OFF)
set(WHISPER_CPP_DIR "" CACHE PATH "Path to a whisper.cpp source checkout")
set(WHISPER_CPP_MODEL_PATH "" CACHE FILEPATH "Optional whisper.cpp model used by integration tests")

if(BUILD_WHISPER_CPP_ASR)
    if(NOT WHISPER_CPP_DIR)
        message(FATAL_ERROR "BUILD_WHISPER_CPP_ASR requires WHISPER_CPP_DIR")
    endif()
    if(NOT EXISTS "${WHISPER_CPP_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "WHISPER_CPP_DIR does not contain a CMake project: ${WHISPER_CPP_DIR}")
    endif()
    add_subdirectory("${WHISPER_CPP_DIR}" "${CMAKE_BINARY_DIR}/third_party/whisper.cpp"
        EXCLUDE_FROM_ALL)
    if(NOT TARGET whisper)
        message(FATAL_ERROR "whisper.cpp did not define the expected whisper target")
    endif()
endif()

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
add_subdirectory(tools/cockpit-ctl)
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
