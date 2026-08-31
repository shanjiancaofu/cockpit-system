#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ros2_install_dir="$(realpath -m "${ROS2_INSTALL_DIR:-${root_dir}/_output/ros2/install}")"
run_dir="$(mktemp -d "${root_dir}/_output/runtime/lidar-nav2-source.XXXXXX")"
launch_log="${run_dir}/launch.log"

if [[ ! -f /opt/ros/humble/setup.bash || ! -f "${ros2_install_dir}/setup.bash" ]]; then
  echo "ROS2 workspace is unavailable" >&2
  exit 2
fi

# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
source "${ros2_install_dir}/setup.bash"
set -u

setsid ros2 launch cockpit_lidar_bringup lidar_nav2.launch.py use_fake:=true \
  >"${launch_log}" 2>&1 &
launch_pid=$!
cleanup() {
  kill -TERM -- "-${launch_pid}" 2>/dev/null || true
  wait "${launch_pid}" 2>/dev/null || true
}
trap cleanup EXIT

publisher_count=0
subscriber_count=0
for _ in $(seq 1 150); do
  topic_info="$(ros2 topic info /scan 2>/dev/null || true)"
  publisher_count="$(awk '/Publisher count:/{print $3}' <<<"${topic_info}")"
  subscriber_count="$(awk '/Subscription count:/{print $3}' <<<"${topic_info}")"
  if [[ "${publisher_count:-0}" == "1" && "${subscriber_count:-0}" -ge 1 ]]; then
    break
  fi
  read -r -t 0.1 _ </dev/null || true
done

if [[ "${publisher_count:-0}" != "1" || "${subscriber_count:-0}" -lt 1 ]]; then
  echo "expected exactly one /scan publisher and at least one Nav2 subscriber" >&2
  sed -n '1,240p' "${launch_log}" >&2
  exit 1
fi

scan="$(timeout -k 1 5 ros2 topic echo --once /scan sensor_msgs/msg/LaserScan)"
frame_id="$(awk '$1 == "frame_id:" {gsub(/[\047\042]/, "", $2); print $2; exit}' <<<"${scan}")"
if [[ "${frame_id}" != "base_scan" ]]; then
  echo "unexpected /scan frame_id: ${frame_id:-missing}" >&2
  exit 1
fi

echo "LiDAR/Nav2 source smoke passed"
echo "publisher_count=${publisher_count} subscriber_count=${subscriber_count} frame_id=${frame_id}"
echo "log=${launch_log}"
