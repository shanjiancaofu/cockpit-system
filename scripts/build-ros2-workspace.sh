#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_dir="${root_dir}/ros2"
output_dir="$(realpath -m "${ROS2_OUTPUT_DIR:-${root_dir}/_output/ros2}")"
build_dir="${output_dir}/build"
install_dir="${output_dir}/install"
log_dir="${output_dir}/log"

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Do not build the ROS2 workspace as root" >&2
  exit 2
fi
if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  echo "ROS 2 Humble is not installed; run scripts/setup-ros2-humble-nav2.sh" >&2
  exit 2
fi
if ! command -v colcon >/dev/null 2>&1; then
  echo "colcon is not installed" >&2
  exit 2
fi

# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
set -u

mkdir -p "${build_dir}" "${install_dir}" "${log_dir}"
colcon --log-base "${log_dir}" build \
  --base-paths "${workspace_dir}/src" \
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
