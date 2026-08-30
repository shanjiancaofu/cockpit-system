"""Run C1 or FakeScan with the existing cockpit Nav2/Safety baseline."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    share = get_package_share_directory("cockpit_lidar_bringup")
    bringup = get_package_share_directory("cockpit_nav2_bringup")
    return LaunchDescription(
        [
            DeclareLaunchArgument("use_fake", default_value="true"),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    os.path.join(share, "launch", "c1.launch.py")
                ),
                launch_arguments={
                    "use_fake": LaunchConfiguration("use_fake")
                }.items(),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    os.path.join(bringup, "launch", "minimal_nav2.launch.py")
                ),
                launch_arguments={
                    "scan_scenario": "empty",
                    "enable_fake_scan": LaunchConfiguration("use_fake"),
                }.items(),
            ),
        ]
    )
