cmake_minimum_required(VERSION 3.8)
project(cockpit_nav2_bringup)

find_package(ament_cmake REQUIRED)

# The workspace exports compile databases globally; this data-only package has no C++ target.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  message(STATUS "compile database export enabled for ROS2 workspace")
endif()

install(DIRECTORY launch maps params DESTINATION share/${PROJECT_NAME})

ament_package()
