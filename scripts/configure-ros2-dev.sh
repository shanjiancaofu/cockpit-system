#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/ros2}")"

if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  echo "ROS 2 Humble is not installed; run scripts/setup-ros2-humble-nav2.sh" >&2
  exit 2
fi

# ROS setup scripts reference optional variables that are valid when unset.
# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
set -u

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
  cockpit/library/bridge/ros2_camera_info_adapter.cc \
  cockpit/library/bridge/ros2_nav2_provider.cc; do
  if ! grep -Fq "${root_dir}/${source}" "${compile_database}"; then
    echo "compile database is missing ${source}" >&2
    exit 1
  fi
done

echo "ROS2 development compile database ready"
echo "build_dir=${build_dir}"
echo "compile_commands=${compile_database}"
echo "Run scripts/build-ros2-workspace.sh to configure ros2/src files for clangd."
