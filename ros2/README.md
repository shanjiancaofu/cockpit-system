# cockpit-system ROS 2 workspace

This workspace contains cockpit-owned ROS 2 launch/configuration and test-only nodes. Official
ROS 2 Humble and Nav2 packages remain installed under `/opt/ros/humble`; their source is not
vendored here.

- `cockpit_nav2_bringup`: bounded Nav2 launch and test map owned by cockpit-system.
- `cockpit_chassis_safety`: production-candidate fail-closed velocity safety adapter; no CAN sink.
- `cockpit_lidar_bringup`: single-source C1/FakeScan selection and C1 parameters.
- `cockpit_nav2_test_support`: non-actuating planar robot fixture for Ubuntu functional tests.

The official `Slamtec/rplidar_ros` ROS2 branch is pinned by
`scripts/setup/ros2/rplidar-ros.env` and prepared outside the repository sources:

```bash
bash scripts/setup/ros2/prepare-rplidar-ros.sh
bash scripts/ros2/build.sh
```

All external source checkouts and colcon build, install, and log outputs remain under
`_output/ros2`.
