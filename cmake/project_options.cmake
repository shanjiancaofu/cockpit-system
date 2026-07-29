include_guard(GLOBAL)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

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

find_package(Git QUIET)
set(COCKPIT_GIT_COMMIT "unknown")
set(COCKPIT_GIT_DIRTY "false")
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
set(COCKPIT_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(COCKPIT_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
set(COCKPIT_COMPILER_PATH "${CMAKE_CXX_COMPILER}")

add_library(cockpit_project_options INTERFACE)
target_include_directories(cockpit_project_options
    INTERFACE
        "${PROJECT_SOURCE_DIR}"
)
if(MSVC)
    target_compile_options(cockpit_project_options INTERFACE /W4)
else()
    target_compile_options(cockpit_project_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()
