include_guard(GLOBAL)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Protobuf 3.12.4 EXACT REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
find_program(COCKPIT_GRPC_CPP_PLUGIN_EXECUTABLE
    NAMES grpc_cpp_plugin
    REQUIRED
)

get_filename_component(COCKPIT_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(COCKPIT_SHERPA_ONNX_SOURCE_DIR
    "${COCKPIT_WORKSPACE_DIR}/third_party/sherpa-onnx"
    CACHE PATH "Sherpa-ONNX source checkout")

if(NOT EXISTS "${COCKPIT_SHERPA_ONNX_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "COCKPIT_SHERPA_ONNX_SOURCE_DIR does not contain a CMake project: "
        "${COCKPIT_SHERPA_ONNX_SOURCE_DIR}")
endif()

set(SHERPA_ONNX_SENSEVOICE_MODEL_PATH "" CACHE FILEPATH
    "Optional SenseVoice ONNX model used by integration tests")
set(SHERPA_ONNX_SENSEVOICE_TEST_WAV_PATH "" CACHE FILEPATH
    "Optional 16 kHz mono WAV used by the SenseVoice integration test")

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
set(SHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

if(DEFINED BUILD_TESTING)
    set(cockpit_build_testing "${BUILD_TESTING}")
else()
    set(cockpit_build_testing ON)
endif()
add_subdirectory(
    "${COCKPIT_SHERPA_ONNX_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/sherpa-onnx"
    EXCLUDE_FROM_ALL
)
set(BUILD_TESTING "${cockpit_build_testing}" CACHE BOOL
    "Build cockpit-system tests" FORCE)
unset(cockpit_build_testing)
if(NOT TARGET sherpa-onnx-c-api)
    message(FATAL_ERROR "sherpa-onnx did not define sherpa-onnx-c-api")
endif()
