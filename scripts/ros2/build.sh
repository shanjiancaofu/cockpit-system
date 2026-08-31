#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
workspace_dir="${root_dir}/ros2"
output_dir="$(realpath -m "${ROS2_OUTPUT_DIR:-${root_dir}/_output/ros2}")"
external_source_dir="$(realpath -m "${ROS2_EXTERNAL_SOURCE_DIR:-${output_dir}/external-src}")"
# shellcheck source=/dev/null
source "${root_dir}/scripts/setup/ros2/rplidar-ros.env"
rplidar_source_dir="${external_source_dir}/rplidar_ros-${RPLIDAR_ROS_REVISION}"
build_dir="${output_dir}/build"
install_dir="${output_dir}/install"
log_dir="${output_dir}/log"

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Do not build the ROS2 workspace as root" >&2
  exit 2
fi
if ! command -v colcon >/dev/null 2>&1; then
  echo "colcon is not installed" >&2
  exit 2
fi
source "${root_dir}/scripts/lib/common.sh"
cockpit_source_ros2_environment required

mkdir -p "${build_dir}" "${install_dir}" "${log_dir}"
base_paths=("${workspace_dir}/src")
if [[ -d "${rplidar_source_dir}/.git" ]]; then
  actual_rplidar_revision="$(git -C "${rplidar_source_dir}" rev-parse HEAD)"
  if [[ "${actual_rplidar_revision}" != "${RPLIDAR_ROS_REVISION}" ]]; then
    echo "rplidar_ros revision mismatch: expected ${RPLIDAR_ROS_REVISION}, got ${actual_rplidar_revision}" >&2
    exit 1
  fi
  base_paths+=("${rplidar_source_dir}")
fi
colcon --log-base "${log_dir}" build \
  --base-paths "${base_paths[@]}" \
  --build-base "${build_dir}" \
  --install-base "${install_dir}" \
  --merge-install \
  --symlink-install \
  --cmake-args \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

compile_database="${build_dir}/cockpit_nav2_test_support/compile_commands.json"
if [[ ! -s "${compile_database}" ]]; then
  echo "missing ROS2 test-support compile database: ${compile_database}" >&2
  exit 1
fi
ln -sfn "${compile_database}" "${workspace_dir}/compile_commands.json"

# shellcheck disable=SC1091
set +u
source "${install_dir}/setup.bash"
set -u
ros2 pkg prefix cockpit_nav2_bringup >/dev/null
ros2 pkg prefix cockpit_nav2_test_support >/dev/null

echo "cockpit ROS2 workspace build passed"
echo "install=${install_dir}"
