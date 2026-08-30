cmake_minimum_required(VERSION 3.8)
project(cockpit_chassis_safety)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

get_filename_component(COCKPIT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_library(chassis_safety_core STATIC
  ${COCKPIT_REPO_ROOT}/cockpit/modules/vehicle/chassis_safety_adapter.cc
)
target_include_directories(chassis_safety_core PUBLIC ${COCKPIT_REPO_ROOT})
target_compile_options(chassis_safety_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(twist_contract STATIC src/twist_contract.cc)
target_include_directories(twist_contract PUBLIC include ${COCKPIT_REPO_ROOT})
target_compile_options(twist_contract PRIVATE -Wall -Wextra -Wpedantic)
ament_target_dependencies(twist_contract geometry_msgs)

add_executable(chassis_safety_adapter src/chassis_safety_adapter_node.cc)
target_link_libraries(chassis_safety_adapter chassis_safety_core twist_contract)
target_include_directories(chassis_safety_adapter PRIVATE include ${COCKPIT_REPO_ROOT})
target_compile_options(chassis_safety_adapter PRIVATE -Wall -Wextra -Wpedantic)
ament_target_dependencies(chassis_safety_adapter geometry_msgs rclcpp std_msgs)

install(TARGETS chassis_safety_adapter DESTINATION lib/${PROJECT_NAME})
install(TARGETS chassis_safety_core ARCHIVE DESTINATION lib)
install(TARGETS twist_contract ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  add_executable(twist_contract_test test/twist_contract_test.cc)
  target_link_libraries(twist_contract_test twist_contract)
  ament_target_dependencies(twist_contract_test geometry_msgs)
  add_test(NAME twist_contract_test COMMAND twist_contract_test)
endif()

ament_package()
