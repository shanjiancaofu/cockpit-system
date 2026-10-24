"""Launch the official Slamtec rplidar_ros C1 driver without modifying it."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory("cockpit_lidar_bringup"),
        "config",
        "c1.yaml",
    )
    return LaunchDescription(
        [
            Node(
                package="rplidar_ros",
                executable="rplidar_node",
                name="rplidar_node",
                output="screen",
                parameters=[config],
                remappings=[("scan", "/scan")],
            ),
        ]
    )
