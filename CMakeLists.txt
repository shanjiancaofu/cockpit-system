cmake_minimum_required(VERSION 3.22)

if(CMAKE_BINARY_DIR STREQUAL "${CMAKE_SOURCE_DIR}/build")
    message(FATAL_ERROR
        "The repository-root build/ directory is not supported. "
        "Use scripts/build.sh or configure under _output/build/<arch>-<type>."
    )
endif()

project(cockpit-system
    VERSION 0.1.0
    DESCRIPTION "Jetson smart cockpit prototype"
    LANGUAGES C CXX
)

include(cmake/project_options.cmake)
include(cmake/dependencies.cmake)

link_libraries(cockpit_project_options)

add_subdirectory(cockpit)
add_subdirectory(agent)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(cmake/install.cmake)
