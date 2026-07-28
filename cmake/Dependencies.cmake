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

set(COCKPIT_SHERPA_REVISION "unknown")
if(Git_FOUND AND EXISTS "${COCKPIT_SHERPA_ONNX_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${COCKPIT_SHERPA_ONNX_SOURCE_DIR}"
        OUTPUT_VARIABLE cockpit_sherpa_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(cockpit_sherpa_revision)
        set(COCKPIT_SHERPA_REVISION "${cockpit_sherpa_revision}")
    endif()
endif()

set(COCKPIT_SENSEVOICE_MODEL_SHA256 "")
if(SHERPA_ONNX_SENSEVOICE_MODEL_PATH AND
   EXISTS "${SHERPA_ONNX_SENSEVOICE_MODEL_PATH}")
    file(SHA256 "${SHERPA_ONNX_SENSEVOICE_MODEL_PATH}"
        COCKPIT_SENSEVOICE_MODEL_SHA256)
endif()

string(TOLOWER "${COCKPIT_TARGET_SYSTEM}" cockpit_package_system)
set(cockpit_git_short "${COCKPIT_GIT_COMMIT}")
string(LENGTH "${cockpit_git_short}" cockpit_git_length)
if(cockpit_git_length GREATER 8)
    string(SUBSTRING "${cockpit_git_short}" 0 8 cockpit_git_short)
endif()
file(WRITE "${CMAKE_BINARY_DIR}/package-info.env"
    "COCKPIT_VERSION='${PROJECT_VERSION}'\n"
    "COCKPIT_BUILD_TYPE='${CMAKE_BUILD_TYPE}'\n"
    "COCKPIT_TARGET_SYSTEM='${COCKPIT_TARGET_SYSTEM}'\n"
    "COCKPIT_PACKAGE_SYSTEM='${cockpit_package_system}'\n"
    "COCKPIT_TARGET_ARCH='${COCKPIT_TARGET_ARCH}'\n"
    "COCKPIT_COMPILER_PATH='${COCKPIT_COMPILER_PATH}'\n"
    "COCKPIT_COMPILER_ID='${COCKPIT_COMPILER_ID}'\n"
    "COCKPIT_COMPILER_VERSION='${COCKPIT_COMPILER_VERSION}'\n"
    "COCKPIT_GIT_REVISION='${COCKPIT_GIT_COMMIT}'\n"
    "COCKPIT_GIT_SHORT='${cockpit_git_short}'\n"
    "COCKPIT_GIT_DIRTY='${COCKPIT_GIT_DIRTY}'\n"
    "COCKPIT_PROTOBUF_VERSION='${Protobuf_VERSION}'\n"
    "COCKPIT_GRPC_VERSION='${GRPCPP_VERSION}'\n"
    "COCKPIT_SHERPA_REVISION='${COCKPIT_SHERPA_REVISION}'\n"
    "COCKPIT_SENSEVOICE_MODEL_SHA256='${COCKPIT_SENSEVOICE_MODEL_SHA256}'\n"
)
