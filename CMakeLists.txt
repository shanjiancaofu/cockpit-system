cmake_minimum_required(VERSION 3.22)

project(cockpit-system
    VERSION 0.1.0
    DESCRIPTION "Jetson smart cockpit prototype"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectSettings)
include(Dependencies)
include(AddCockpitLibrary)

add_subdirectory(cockpit)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(Install)
