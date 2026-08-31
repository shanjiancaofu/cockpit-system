#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/../../.." && pwd)"
# shellcheck source=/dev/null
source "${script_dir}/rplidar-ros.env"

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Do not prepare rplidar_ros as root" >&2
  exit 2
fi

external_root="$(realpath -m "${ROS2_EXTERNAL_SOURCE_DIR:-${root_dir}/_output/ros2/external-src}")"
destination="${external_root}/rplidar_ros-${RPLIDAR_ROS_REVISION}"

if [[ -d "${destination}/.git" ]]; then
  actual="$(git -C "${destination}" rev-parse HEAD)"
  if [[ "${actual}" != "${RPLIDAR_ROS_REVISION}" ]]; then
    echo "rplidar_ros revision mismatch: expected ${RPLIDAR_ROS_REVISION}, got ${actual}" >&2
    exit 1
  fi
  echo "Reusing pinned rplidar_ros ${actual}"
  exit 0
fi
if [[ -e "${destination}" ]]; then
  echo "rplidar_ros destination exists but is not a Git checkout: ${destination}" >&2
  exit 1
fi

mkdir -p "${external_root}"
temporary="$(mktemp -d "${external_root}/.rplidar_ros.XXXXXX")"
cleanup() {
  if [[ -d "${temporary}" ]]; then
    rm -rf -- "${temporary}"
  fi
}
trap cleanup EXIT

git -C "${temporary}" init --quiet
git -C "${temporary}" remote add origin "${RPLIDAR_ROS_REPOSITORY}"
git -C "${temporary}" fetch --quiet --depth=1 origin "${RPLIDAR_ROS_REVISION}"
git -C "${temporary}" checkout --quiet --detach FETCH_HEAD
actual="$(git -C "${temporary}" rev-parse HEAD)"
if [[ "${actual}" != "${RPLIDAR_ROS_REVISION}" ]]; then
  echo "downloaded rplidar_ros revision mismatch: expected ${RPLIDAR_ROS_REVISION}, got ${actual}" >&2
  exit 1
fi

mv -- "${temporary}" "${destination}"
trap - EXIT
echo "Prepared pinned rplidar_ros ${actual}"
echo "source=${destination}"
