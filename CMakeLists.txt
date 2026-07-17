cmake_minimum_required(VERSION 3.16)

project(cockpit-system
    VERSION 0.1.0
    DESCRIPTION "Jetson smart cockpit prototype"
    LANGUAGES CXX
)

find_package(Git QUIET)
set(COCKPIT_GIT_COMMIT "unknown")
set(COCKPIT_GIT_DIRTY "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE cockpit_git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(cockpit_git_commit)
        set(COCKPIT_GIT_COMMIT "${cockpit_git_commit}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff --quiet
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE cockpit_git_dirty_result
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff --cached --quiet
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE cockpit_git_cached_dirty_result
        ERROR_QUIET
    )
    if(cockpit_git_dirty_result EQUAL 0 AND cockpit_git_cached_dirty_result EQUAL 0)
        set(COCKPIT_GIT_DIRTY "false")
    else()
        set(COCKPIT_GIT_DIRTY "true")
    endif()
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cockpit_system_processor)
if(cockpit_system_processor MATCHES "^(aarch64|arm64)$")
    set(cockpit_target_arch "arm64")
elseif(cockpit_system_processor MATCHES "^(x86_64|amd64)$")
    set(cockpit_target_arch "x86_64")
else()
    set(cockpit_target_arch "${cockpit_system_processor}")
endif()
set(COCKPIT_TARGET_ARCH "${cockpit_target_arch}" CACHE STRING
    "Normalized architecture used for deployment packages")
set(COCKPIT_TARGET_SYSTEM "${CMAKE_SYSTEM_NAME}" CACHE STRING
    "Target operating system used for deployment packages")
set(COCKPIT_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}" CACHE STRING
    "Compiler family used for this build" FORCE)
set(COCKPIT_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}" CACHE STRING
    "Compiler version used for this build" FORCE)
set(COCKPIT_COMPILER_PATH "${CMAKE_CXX_COMPILER}" CACHE FILEPATH
    "Compiler executable used for this build" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

option(BUILD_COCKPIT_UI "Build Qt cockpit UI" ON)
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

add_subdirectory(cockpit)
add_subdirectory(tools/diagnostics)
add_subdirectory(tools/can-simulator)
add_subdirectory(tools/cockpit-ctl)
add_subdirectory(tools/recording-ctl)
add_subdirectory(tools/safe-ota)
add_subdirectory(tools/audio-probe)
add_subdirectory(tools/camera-ctl)
add_subdirectory(tools/camera-probe)
add_subdirectory(tools/camera-preview-probe)
add_subdirectory(tools/voice-ctl)
add_subdirectory(tools/topic)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
