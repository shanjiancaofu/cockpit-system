# Nav2 parameters

The Ubuntu baseline intentionally uses the exact upstream
`/opt/ros/humble/share/nav2_bringup/params/nav2_params.yaml` supplied by the pinned
`ros-humble-nav2-bringup` deb. This directory is reserved for reviewed, robot-specific overrides
after real geometry, lidar, odometry, and controller limits are measured.

Do not add invented production footprint, acceleration, sensor range, or controller values here.
