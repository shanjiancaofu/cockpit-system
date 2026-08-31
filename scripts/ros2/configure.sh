#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"
build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/ros2}")"

cockpit_source_ros2_environment required

cmake -S "${root_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCOCKPIT_ENABLE_ROS2=ON

compile_database="${build_dir}/compile_commands.json"
if [[ ! -s "${compile_database}" ]]; then
  echo "CMake did not generate ${compile_database}" >&2
  exit 1
fi

ln -sfn "${compile_database}" "${root_dir}/compile_commands.json"

for source in \
  cockpit/library/bridge/ros2_camera_frame_adapter.cc \
  cockpit/library/bridge/ros2_camera_info_adapter.cc \
  cockpit/library/bridge/ros2_camera_publisher.cc \
  cockpit/library/bridge/ros2_chassis_odometry_adapter.cc \
  cockpit/library/bridge/ros2_chassis_odometry_publisher.cc \
  cockpit/library/bridge/ros2_nav2_provider.cc; do
  if ! grep -Fq "${root_dir}/${source}" "${compile_database}"; then
    echo "compile database is missing ${source}" >&2
    exit 1
  fi
done

echo "ROS2 development compile database ready"
echo "build_dir=${build_dir}"
echo "compile_commands=${compile_database}"
echo "Run scripts/ros2/build.sh to configure ros2/src files for clangd."
