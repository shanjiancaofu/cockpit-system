cmake_minimum_required(VERSION 3.16)

project(cockpit-system
    VERSION 0.1.0
    DESCRIPTION "Jetson smart cockpit prototype"
    LANGUAGES C CXX
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

option(BUILD_WEB_DASHBOARD "Build web dashboard" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(COCKPIT_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(COCKPIT_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(COCKPIT_ENABLE_ASAN_UBSAN AND COCKPIT_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled in the same build")
endif()
if(COCKPIT_ENABLE_ASAN_UBSAN)
    add_compile_options(-fno-omit-frame-pointer -fsanitize=address,undefined)
    add_link_options(-fno-omit-frame-pointer -fsanitize=address,undefined)
elseif(COCKPIT_ENABLE_TSAN)
    add_compile_options(-fno-omit-frame-pointer -fsanitize=thread)
    add_link_options(-fno-omit-frame-pointer -fsanitize=thread)
endif()
get_filename_component(COCKPIT_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(COCKPIT_UNIFIED_DEPS_PREFIX
    "${COCKPIT_WORKSPACE_DIR}/third_party/cockpit-unified-deps/prefix"
    CACHE PATH "Path to the cockpit Protobuf 3.21.12 and gRPC 1.51.3 prefix")
if(NOT SHERPA_ONNX_DIR)
    set(SHERPA_ONNX_DIR "${COCKPIT_WORKSPACE_DIR}/third_party/sherpa-onnx" CACHE PATH
        "Path to the sherpa-onnx source checkout" FORCE)
endif()
set(SHERPA_ONNX_SENSEVOICE_MODEL_PATH "" CACHE FILEPATH
    "Optional SenseVoice ONNX model used by integration tests")
set(SHERPA_ONNX_SENSEVOICE_TEST_WAV_PATH "" CACHE FILEPATH
    "Optional 16 kHz mono WAV used by the SenseVoice integration test")

if(NOT EXISTS "${COCKPIT_UNIFIED_DEPS_PREFIX}/lib/cmake/protobuf")
    message(FATAL_ERROR
        "cockpit unified dependencies not found: ${COCKPIT_UNIFIED_DEPS_PREFIX}")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${COCKPIT_UNIFIED_DEPS_PREFIX}")

if(NOT EXISTS "${SHERPA_ONNX_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SHERPA_ONNX_DIR does not contain a CMake project: ${SHERPA_ONNX_DIR}")
endif()

set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_LINK_LIBSTDCPP_STATICALLY OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SHERPA_ONNX_DIR}" "${CMAKE_BINARY_DIR}/third_party/sherpa-onnx"
    EXCLUDE_FROM_ALL)
if(NOT TARGET sherpa-onnx-c-api)
    message(FATAL_ERROR "sherpa-onnx did not define the expected sherpa-onnx-c-api target")
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
