add_library(bridge_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/bridge_grpc_service.cc
    ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/bridge_runtime.cc
)
target_include_directories(bridge_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(bridge_runtime PUBLIC config contracts logging bridge time_util)
target_compile_definitions(bridge_runtime PRIVATE COCKPIT_ENABLE_ROS2=$<BOOL:${COCKPIT_ENABLE_ROS2}>)

if(COCKPIT_ENABLE_ROS2)
    add_library(bridge_ros2 STATIC
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_camera_info_adapter.cc
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_camera_frame_adapter.cc
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_camera_publisher.cc
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_chassis_odometry_adapter.cc
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_chassis_odometry_publisher.cc
        ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/ros2_nav2_provider.cc
    )
    find_package(OpenCV REQUIRED COMPONENTS calib3d imgproc)
    target_include_directories(bridge_ros2 PUBLIC ${PROJECT_SOURCE_DIR})
    target_link_libraries(bridge_ros2 bridge hawkeye vehicle ${OpenCV_LIBS})
    ament_target_dependencies(bridge_ros2
        builtin_interfaces geometry_msgs nav2_msgs nav_msgs rclcpp rclcpp_action sensor_msgs
    )
    target_link_libraries(bridge_runtime PRIVATE bridge_ros2)
endif()

