#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  echo "ROS 2 Humble is not installed; run scripts/setup-ros2-humble-nav2.sh" >&2
  exit 2
fi
# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
set -u

build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/ros2}")"
bin_dir="${build_dir}/bin"
module_dir="${build_dir}/lib/cockpit/modules"
source_config="$(realpath "${CONFIG_PATH:-${root_dir}/configs/development.yaml}")"
run_dir="${COCKPIT_RUNTIME_DIR:-${root_dir}/_output/runtime}/ros2-nav2-smoke-${BASHPID}"
config_path="${run_dir}/config.yaml"
socket_path="${run_dir}/navigator.sock"
bridge_socket="${run_dir}/bridge.grpc.sock"
vehicle_socket="${run_dir}/vehicle.grpc.sock"
gateway_socket="${run_dir}/gateway.grpc.sock"
action_name="/cockpit_smoke_navigate_to_pose_${BASHPID}"
navigator_log="${run_dir}/navigator.log"
nav2_log="${run_dir}/fake-nav2.log"

for executable in cockpit-navigator cockpit-ctl bridge-ctl fake_nav2_action_server; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}; build with COCKPIT_ENABLE_ROS2=ON" >&2
    exit 2
  fi
done
for module in bridge transfer vehicle_driver carupload; do
  if [[ ! -f "${module_dir}/lib${module}.so" ]]; then
    echo "missing ${module_dir}/lib${module}.so" >&2
    exit 2
  fi
done

mkdir -p "${run_dir}"
export COCKPIT_RUNTIME_DIR="${run_dir}"
awk -v action_name="${action_name}" -v bridge_socket="${bridge_socket}" \
    -v vehicle_socket="${vehicle_socket}" -v gateway_socket="${gateway_socket}" '
  /^  vehicle_data:$/ { in_vehicle = 1 }
  in_vehicle && /^      listen_address:/ {
    sub(/listen_address:.*/, "listen_address: unix:" vehicle_socket)
    in_vehicle = 0
  }
  /^  gateway:$/ { in_gateway = 1 }
  in_gateway && /^    vehicle_data_address:/ {
    sub(/vehicle_data_address:.*/, "vehicle_data_address: unix:" vehicle_socket)
  }
  in_gateway && /^      listen_address:/ {
    sub(/listen_address:.*/, "listen_address: unix:" gateway_socket)
    in_gateway = 0
  }
  /^  bridge:$/ { in_bridge = 1 }
  in_bridge && /^    provider:/ { sub(/provider:.*/, "provider: ros2_nav2") }
  in_bridge && /^    nav2_action_name:/ {
    sub(/nav2_action_name:.*/, "nav2_action_name: " action_name)
  }
  in_bridge && /^    nav2_server_timeout_ms:/ {
    sub(/nav2_server_timeout_ms:.*/, "nav2_server_timeout_ms: 750")
  }
  in_bridge && /^      listen_address:/ {
    sub(/listen_address:.*/, "listen_address: unix:" bridge_socket)
    in_bridge = 0
  }
  { print }
' "${source_config}" >"${config_path}"

navigator_pid=""
nav2_pid=""
cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${bin_dir}/cockpit-navigator" --command shutdown --socket "${socket_path}" \
      >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${nav2_pid}" ]] && kill -0 "${nav2_pid}" >/dev/null 2>&1; then
    kill "${nav2_pid}" >/dev/null 2>&1 || true
    wait "${nav2_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

start_nav2() {
  "${bin_dir}/fake_nav2_action_server" --action-name "${action_name}" \
    >>"${nav2_log}" 2>&1 &
  nav2_pid=$!
  for _ in $(seq 1 100); do
    if ros2 action list 2>/dev/null | grep -Fxq "${action_name}"; then
      return 0
    fi
    if ! kill -0 "${nav2_pid}" >/dev/null 2>&1; then
      echo "fake Nav2 server exited; see ${nav2_log}" >&2
      return 1
    fi
    sleep 0.05
  done
  echo "fake Nav2 action did not become discoverable" >&2
  return 1
}

wait_for_bridge_state() {
  local expected="$1"
  local require_pose="${2:-false}"
  local status
  for _ in $(seq 1 100); do
    status="$("${bin_dir}/bridge-ctl" --status --output json --config "${config_path}" \
      2>/dev/null || true)"
    if [[ "${status}" == *"\"state\":\"${expected}\""* ]]; then
      if [[ "${require_pose}" != "true" || "${status}" == *'"current_pose_valid":true'* ]]; then
        return 0
      fi
    fi
    sleep 0.05
  done
  echo "bridge did not reach ${expected}; last status: ${status:-unavailable}" >&2
  return 1
}

submit_until_accepted() {
  local goal_prefix="$1"
  local x="$2"
  local y="$3"
  local yaw="$4"
  local response
  for attempt in $(seq 1 10); do
    response="$("${bin_dir}/bridge-ctl" --submit --goal-id "${goal_prefix}-${attempt}" \
      --x "${x}" --y "${y}" --yaw "${yaw}" --output json --config "${config_path}" \
      2>/dev/null || true)"
    if [[ "${response}" == *'"state":"NAVIGATION_STATE_ACCEPTED"'* ||
          "${response}" == *'"state":"NAVIGATION_STATE_EXECUTING"'* ]]; then
      submitted_goal_id="${goal_prefix}-${attempt}"
      return 0
    fi
    sleep 0.1
  done
  echo "Bridge did not accept ${goal_prefix}; last response: ${response:-unavailable}" >&2
  return 1
}

start_nav2
(
  cd "${run_dir}"
  exec "${bin_dir}/cockpit-navigator" --config "${config_path}" --module-dir "${module_dir}" \
    --socket "${socket_path}" --mode cloud
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

for _ in $(seq 1 100); do
  if "${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    echo "Navigator exited; see ${navigator_log}" >&2
    exit 1
  fi
  sleep 0.05
done
for module in carupload vehicle_driver transfer; do
  "${bin_dir}/cockpit-ctl" runtime stop "${module}" --socket "${socket_path}" >/dev/null
done
"${bin_dir}/cockpit-ctl" runtime start bridge --socket "${socket_path}" >/dev/null
runtime_status="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
if [[ "${runtime_status}" != *"module=bridge state=running"* ]]; then
  echo "Navigator did not start bridge module" >&2
  exit 1
fi
wait_for_bridge_state NAVIGATION_STATE_IDLE

submitted_goal_id=""
submit_until_accepted ros2-smoke-success 2 1 0.5
wait_for_bridge_state NAVIGATION_STATE_EXECUTING true
wait_for_bridge_state NAVIGATION_STATE_SUCCEEDED true

submit_until_accepted ros2-smoke-cancel -1 0 0
wait_for_bridge_state NAVIGATION_STATE_EXECUTING true
"${bin_dir}/bridge-ctl" --cancel --goal-id "${submitted_goal_id}" --config "${config_path}" \
  >/dev/null
wait_for_bridge_state NAVIGATION_STATE_CANCELLED

kill "${nav2_pid}"
wait "${nav2_pid}"
nav2_pid=""
if "${bin_dir}/bridge-ctl" --submit --goal-id ros2-smoke-disconnected \
  --x 1 --y 0 --yaw 0 --config "${config_path}" >/dev/null 2>&1; then
  echo "bridge unexpectedly accepted a goal without Nav2" >&2
  exit 1
fi
disconnected="$("${bin_dir}/bridge-ctl" --status --output json --config "${config_path}")"
if [[ "${disconnected}" != *'"state":"NAVIGATION_STATE_DISCONNECTED"'* ]]; then
  echo "bridge did not retain Nav2 disconnection: ${disconnected}" >&2
  exit 1
fi

start_nav2
wait_for_bridge_state NAVIGATION_STATE_IDLE
submit_until_accepted ros2-smoke-recovery 3 1 0
wait_for_bridge_state NAVIGATION_STATE_SUCCEEDED true

"${bin_dir}/cockpit-navigator" --command shutdown --socket "${socket_path}" >/dev/null
wait "${navigator_pid}"
navigator_pid=""
echo "ROS2 Nav2 Bridge full-process smoke passed"
