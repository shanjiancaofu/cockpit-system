#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$(realpath -m "${ROS2_OUTPUT_DIR:-${root_dir}/_output/ros2}")"
main_build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/ros2}")"

if [[ ! -f /opt/ros/humble/setup.bash || ! -f "${output_dir}/install/setup.bash" ]]; then
  echo "ROS2 workspace is unavailable; run setup and build-ros2-workspace.sh" >&2
  exit 2
fi

# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
source "${output_dir}/install/setup.bash"
set -u

colcon --log-base "${output_dir}/log" test \
  --base-paths "${root_dir}/ros2/src" \
  --build-base "${output_dir}/build" \
  --install-base "${output_dir}/install" \
  --merge-install \
  --event-handlers console_direct+
colcon --log-base "${output_dir}/log" test-result \
  --test-result-base "${output_dir}/build" \
  --verbose

BUILD_DIR="${main_build_dir}" ROS2_INSTALL_DIR="${output_dir}/install" \
  bash "${root_dir}/scripts/tests/nav2-minimal-bridge-smoke.sh"
BUILD_DIR="${main_build_dir}" \
  bash "${root_dir}/scripts/tests/ros2-nav2-bridge-smoke.sh"

echo "cockpit ROS2 workspace tests passed"
