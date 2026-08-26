#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ros2-humble-pins.env
source "${script_dir}/ros2-humble-pins.env"

verify_package() {
  local package="$1"
  local expected="$2"
  local actual
  actual="$(dpkg-query -W -f='${Version}' "${package}" 2>/dev/null || true)"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "${package} version mismatch: expected ${expected}, got ${actual:-not installed}" >&2
    exit 1
  fi
}

verify_package ros-humble-ros-base "${ROS_BASE_VERSION}"
verify_package ros-humble-rclcpp "${RCLCPP_VERSION}"
verify_package ros-humble-rclcpp-action "${RCLCPP_ACTION_VERSION}"
verify_package ros-humble-nav2-msgs "${NAV2_MSGS_VERSION}"
verify_package ros-humble-navigation2 "${NAVIGATION2_VERSION}"
verify_package ros-humble-nav2-bringup "${NAV2_BRINGUP_VERSION}"
verify_package ros-humble-sensor-msgs "${SENSOR_MSGS_VERSION}"
verify_package ros-humble-geometry-msgs "${GEOMETRY_MSGS_VERSION}"
verify_package ros-humble-builtin-interfaces "${BUILTIN_INTERFACES_VERSION}"
verify_package ros-humble-lifecycle-msgs "${LIFECYCLE_MSGS_VERSION}"
verify_package ros-humble-nav-msgs "${NAV_MSGS_VERSION}"
verify_package ros-humble-std-msgs "${STD_MSGS_VERSION}"
verify_package ros-humble-tf2-ros "${TF2_ROS_VERSION}"
verify_package ros-humble-tf2-geometry-msgs "${TF2_GEOMETRY_MSGS_VERSION}"
verify_package ros-humble-nav2-map-server "${NAV2_MAP_SERVER_VERSION}"
verify_package ros-humble-nav2-lifecycle-manager "${NAV2_LIFECYCLE_MANAGER_VERSION}"
verify_package ros-humble-nav2-controller "${NAV2_CONTROLLER_VERSION}"
verify_package ros-humble-nav2-smoother "${NAV2_SMOOTHER_VERSION}"
verify_package ros-humble-nav2-planner "${NAV2_PLANNER_VERSION}"
verify_package ros-humble-nav2-behaviors "${NAV2_BEHAVIORS_VERSION}"
verify_package ros-humble-nav2-bt-navigator "${NAV2_BT_NAVIGATOR_VERSION}"
verify_package ros-humble-nav2-waypoint-follower "${NAV2_WAYPOINT_FOLLOWER_VERSION}"
verify_package ros-humble-nav2-velocity-smoother "${NAV2_VELOCITY_SMOOTHER_VERSION}"
verify_package ros-humble-nav2-dwb-controller "${NAV2_DWB_CONTROLLER_VERSION}"
verify_package ros-humble-nav2-navfn-planner "${NAV2_NAVFN_PLANNER_VERSION}"
verify_package ros-humble-nav2-costmap-2d "${NAV2_COSTMAP_2D_VERSION}"

echo "ROS 2 Humble/Nav2 pinned package verification passed"
