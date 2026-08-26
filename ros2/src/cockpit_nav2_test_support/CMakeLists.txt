cmake_minimum_required(VERSION 3.8)
project(cockpit_nav2_test_support)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)

add_executable(fake_odometry_node src/fake_odometry_node.cc)
ament_target_dependencies(fake_odometry_node
  geometry_msgs nav_msgs rclcpp std_srvs tf2 tf2_geometry_msgs tf2_ros
)

add_executable(fake_tf_node src/fake_tf_node.cc)
ament_target_dependencies(fake_tf_node geometry_msgs rclcpp tf2_ros)

add_executable(fake_scan_node src/fake_scan_node.cc)
ament_target_dependencies(fake_scan_node rclcpp sensor_msgs)

add_executable(fake_cmd_vel_sink src/fake_cmd_vel_sink.cc)
ament_target_dependencies(fake_cmd_vel_sink geometry_msgs rclcpp std_msgs)

add_executable(fake_cmd_vel_safety_adapter src/fake_cmd_vel_safety_adapter.cc)
target_include_directories(fake_cmd_vel_safety_adapter PRIVATE include)
target_link_libraries(fake_cmd_vel_safety_adapter cmd_vel_safety)
ament_target_dependencies(fake_cmd_vel_safety_adapter geometry_msgs rclcpp)

add_library(cmd_vel_safety STATIC src/cmd_vel_safety.cc)
target_include_directories(cmd_vel_safety PUBLIC include)
ament_target_dependencies(cmd_vel_safety geometry_msgs rclcpp)
if(BUILD_TESTING)
  add_executable(cmd_vel_safety_test test/cmd_vel_safety_test.cc)
  target_link_libraries(cmd_vel_safety_test cmd_vel_safety)
  ament_target_dependencies(cmd_vel_safety_test geometry_msgs rclcpp)
endif()

add_executable(nav2_readiness_probe src/nav2_readiness_probe.cc)
ament_target_dependencies(nav2_readiness_probe lifecycle_msgs nav2_msgs rclcpp rclcpp_action)

add_executable(nav2_fault_control src/nav2_fault_control.cc)
ament_target_dependencies(nav2_fault_control rclcpp std_msgs std_srvs)

foreach(target fake_odometry_node fake_tf_node fake_scan_node fake_cmd_vel_sink fake_cmd_vel_safety_adapter nav2_readiness_probe nav2_fault_control)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

install(
  TARGETS fake_odometry_node fake_tf_node fake_scan_node fake_cmd_vel_sink fake_cmd_vel_safety_adapter nav2_readiness_probe nav2_fault_control
  DESTINATION lib/${PROJECT_NAME}
)

install(TARGETS cmd_vel_safety ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
if(BUILD_TESTING)
  add_test(NAME cmd_vel_safety_test COMMAND cmd_vel_safety_test)
endif()

ament_package()
