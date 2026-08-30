"""Launch a non-actuating Nav2 functional baseline for cockpit-system."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    cockpit_share = get_package_share_directory("cockpit_nav2_bringup")
    nav2_share = get_package_share_directory("nav2_bringup")
    default_map = os.path.join(cockpit_share, "maps", "minimal_test_map.yaml")
    upstream_params = os.path.join(nav2_share, "params", "nav2_params.yaml")

    map_file = LaunchConfiguration("map")
    params_file = LaunchConfiguration("params_file")
    scan_scenario = LaunchConfiguration("scan_scenario")
    enable_fake_scan = LaunchConfiguration("enable_fake_scan")

    return LaunchDescription(
        [
            DeclareLaunchArgument("map", default_value=default_map),
            DeclareLaunchArgument(
                "params_file", default_value=upstream_params
            ),
            DeclareLaunchArgument("scan_scenario", default_value="empty"),
            DeclareLaunchArgument("enable_fake_scan", default_value="true"),
            Node(
                package="cockpit_nav2_test_support",
                executable="fake_odometry_node",
                name="cockpit_fake_odometry",
                output="screen",
            ),
            Node(
                package="cockpit_nav2_test_support",
                executable="fake_tf_node",
                name="cockpit_fake_tf",
                output="screen",
            ),
            Node(
                package="cockpit_nav2_test_support",
                executable="fake_scan_node",
                name="cockpit_fake_scan",
                output="screen",
                parameters=[{"scenario": scan_scenario}],
                condition=IfCondition(enable_fake_scan),
            ),
            Node(
                package="cockpit_chassis_safety",
                executable="chassis_safety_adapter",
                name="cockpit_chassis_safety_adapter",
                output="screen",
                parameters=[
                    {
                        "enabled": True,
                        "authority_granted": True,
                        "emergency_stop": False,
                        "allow_test_state_override": True,
                        "test_peer_alive": True,
                        "test_chassis_fault": False,
                        "peer_timeout_ms": 300,
                        "fault_state_timeout_ms": 300,
                        "max_linear_velocity_mm_s": 400,
                        "max_angular_velocity_mrad_s": 1200,
                        "max_linear_acceleration_mm_s2": 400,
                        "max_angular_acceleration_mrad_s2": 1200,
                        "command_timeout_ms": 250,
                        "output_period_ms": 20,
                    }
                ],
            ),
            Node(
                package="cockpit_nav2_test_support",
                executable="fake_chassis_sink",
                name="cockpit_fake_chassis_sink",
                output="screen",
            ),
            Node(
                package="nav2_map_server",
                executable="map_server",
                name="map_server",
                output="screen",
                parameters=[
                    {"yaml_filename": map_file, "use_sim_time": False}
                ],
            ),
            Node(
                package="nav2_lifecycle_manager",
                executable="lifecycle_manager",
                name="lifecycle_manager_map",
                output="screen",
                parameters=[
                    {
                        "autostart": True,
                        "node_names": ["map_server"],
                        "use_sim_time": False,
                    }
                ],
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    os.path.join(nav2_share, "launch", "navigation_launch.py")
                ),
                launch_arguments={
                    "use_sim_time": "false",
                    "autostart": "true",
                    "params_file": params_file,
                    "use_composition": "False",
                    "use_respawn": "False",
                    "log_level": "info",
                }.items(),
            ),
        ]
    )
