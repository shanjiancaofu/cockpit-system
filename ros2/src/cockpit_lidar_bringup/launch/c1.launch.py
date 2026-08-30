"""Launch the official Slamtec rplidar_ros C1 driver without modifying it."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(get_package_share_directory("cockpit_lidar_bringup"), "config", "c1.yaml")
    use_fake = LaunchConfiguration("use_fake")
    return LaunchDescription([
        DeclareLaunchArgument("use_fake", default_value="false"),
        Node(
            package="rplidar_ros",
            executable="rplidarNode",
            name="rplidar_node",
            output="screen",
            parameters=[config],
            remappings=[("scan", "/scan")],
            condition=UnlessCondition(use_fake),
        ),
        Node(
            package="cockpit_nav2_test_support",
            executable="fake_scan_node",
            name="cockpit_fake_scan",
            output="screen",
            parameters=[{"scenario": "empty"}],
            condition=IfCondition(use_fake),
        ),
    ])
