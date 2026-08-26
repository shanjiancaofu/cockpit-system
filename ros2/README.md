# cockpit-system ROS 2 workspace

This workspace contains cockpit-owned ROS 2 launch/configuration and test-only nodes. Official
ROS 2 Humble and Nav2 packages remain installed under `/opt/ros/humble`; their source is not
vendored here.

- `cockpit_nav2_bringup`: bounded Nav2 launch and test map owned by cockpit-system.
- `cockpit_nav2_test_support`: non-actuating planar robot fixture for Ubuntu functional tests.

All colcon build, install, and log outputs must remain under `_output/ros2`.
