#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

main_build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/ros2}")"
ros2_install="$(realpath -m "${ROS2_INSTALL_DIR:-${root_dir}/_output/ros2/install}")"
bin_dir="${main_build_dir}/bin"
module_dir="${main_build_dir}/lib/cockpit/modules"
source_config="$(realpath "${CONFIG_PATH:-${root_dir}/configs/development.yaml}")"
run_dir="${COCKPIT_RUNTIME_DIR:-${root_dir}/_output/runtime}/nav2-minimal-smoke-${BASHPID}"
config_path="${run_dir}/config.yaml"
socket_path="${run_dir}/navigator.sock"
bridge_socket="${run_dir}/bridge.grpc.sock"
vehicle_socket="${run_dir}/vehicle.grpc.sock"
gateway_socket="${run_dir}/gateway.grpc.sock"
navigator_log="${run_dir}/navigator.log"
nav2_log="${run_dir}/nav2.log"

if [[ ! -f /opt/ros/humble/setup.bash || ! -f "${ros2_install}/setup.bash" ]]; then
  echo "ROS2/Nav2 workspace is unavailable; run setup and build-ros2-workspace.sh" >&2
  exit 2
fi
# shellcheck disable=SC1091
set +u
source /opt/ros/humble/setup.bash
source "${ros2_install}/setup.bash"
set -u

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((100 + BASHPID % 100))}"
export COCKPIT_RUNTIME_DIR="${run_dir}"

for executable in cockpit-navigator cockpit-ctl bridge-ctl; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}" >&2
    exit 2
  fi
done
for module in bridge transfer vehicle_driver carupload; do
  if [[ ! -f "${module_dir}/lib${module}.so" ]]; then
    echo "missing ${module_dir}/lib${module}.so" >&2
    exit 2
  fi
done
ros2 pkg prefix cockpit_nav2_bringup >/dev/null
ros2 pkg prefix cockpit_nav2_test_support >/dev/null

mkdir -p "${run_dir}"
awk -v bridge_socket="${bridge_socket}" -v vehicle_socket="${vehicle_socket}" \
    -v gateway_socket="${gateway_socket}" '
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
    sub(/nav2_action_name:.*/, "nav2_action_name: /navigate_to_pose")
  }
  in_bridge && /^    nav2_server_timeout_ms:/ {
    sub(/nav2_server_timeout_ms:.*/, "nav2_server_timeout_ms: 1000")
  }
  in_bridge && /^    goal_timeout_ms:/ {
    sub(/goal_timeout_ms:.*/, "goal_timeout_ms: 8000")
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
  stop_nav2
}

stop_nav2() {
  if [[ -n "${nav2_pid}" ]] && kill -0 "${nav2_pid}" >/dev/null 2>&1; then
    kill -INT -- "-${nav2_pid}" >/dev/null 2>&1 || true
    for _ in $(seq 1 100); do
      if ! kill -0 "${nav2_pid}" >/dev/null 2>&1; then
        break
      fi
      sleep 0.05
    done
    if kill -0 "${nav2_pid}" >/dev/null 2>&1; then
      kill -TERM -- "-${nav2_pid}" >/dev/null 2>&1 || true
    fi
    wait "${nav2_pid}" >/dev/null 2>&1 || true
  fi
  nav2_pid=""
}

start_nav2() {
  setsid ros2 launch cockpit_nav2_bringup minimal_nav2.launch.py >>"${nav2_log}" 2>&1 &
  nav2_pid=$!
  if ! timeout -k 1 30 ros2 run cockpit_nav2_test_support nav2_readiness_probe; then
    echo "Nav2 lifecycle/action readiness failed; see ${nav2_log}" >&2
    return 1
  fi
}
trap cleanup EXIT

: >"${nav2_log}"
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

bridge_status_json() {
  "${bin_dir}/bridge-ctl" --status --output json --config "${config_path}" 2>/dev/null || true
}

wait_for_bridge_state() {
  local expected="$1"
  local require_pose="${2:-false}"
  local status=""
  for _ in $(seq 1 300); do
    status="$(bridge_status_json)"
    if [[ "${status}" == *"\"state\":\"${expected}\""* ]]; then
      if [[ "${require_pose}" != "true" || "${status}" == *'"current_pose_valid":true'* ]]; then
        printf '%s' "${status}"
        return 0
      fi
    fi
    sleep 0.05
  done
  echo "Bridge did not reach ${expected}; last status: ${status:-unavailable}" >&2
  return 1
}

wait_for_bridge_failure() {
  local status=""
  for _ in $(seq 1 300); do
    status="$(bridge_status_json)"
    if [[ "${status}" == *'"state":"NAVIGATION_STATE_FAILED"'* ||
          "${status}" == *'"state":"NAVIGATION_STATE_TIMED_OUT"'* ]]; then
      printf '%s' "${status}"
      return 0
    fi
    sleep 0.05
  done
  echo "Bridge did not reach FAILED/TIMED_OUT; last status: ${status:-unavailable}" >&2
  return 1
}

assert_cmd_vel_zero() {
  timeout -k 1 5 ros2 run cockpit_nav2_test_support nav2_fault_control assert-cmd-zero
}

wait_for_bridge_state NAVIGATION_STATE_IDLE >/dev/null
"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-success --x 0.8 --y 0 --yaw 0 \
  --config "${config_path}" >/dev/null
wait_for_bridge_state NAVIGATION_STATE_EXECUTING true >/dev/null
succeeded="$(wait_for_bridge_state NAVIGATION_STATE_SUCCEEDED true)"
python3 -c '
import json, sys
status = json.load(sys.stdin)
pose = status["current_pose"]
assert status["current_pose_valid"] is True
assert int(pose["timestamp_ms"]) > 0
assert float(pose["x_m"]) > 0.4
' <<<"${succeeded}"
assert_cmd_vel_zero

command_count="$(timeout -k 1 5 ros2 topic echo --once \
  /cockpit_nav2_test_support/cmd_vel_count std_msgs/msg/UInt64 | awk '/data:/{print $2; exit}')"
if [[ -z "${command_count}" || "${command_count}" -le 0 ]]; then
  echo "Nav2 did not produce bounded fake cmd_vel evidence" >&2
  exit 1
fi

"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-cancel --x 1.5 --y 0 --yaw 0 \
  --config "${config_path}" >/dev/null
wait_for_bridge_state NAVIGATION_STATE_EXECUTING true >/dev/null
"${bin_dir}/bridge-ctl" --cancel --goal-id nav2-minimal-cancel --config "${config_path}" \
  >/dev/null
wait_for_bridge_state NAVIGATION_STATE_CANCELLED >/dev/null
assert_cmd_vel_zero

"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-unreachable --x 10 --y 0 --yaw 0 \
  --config "${config_path}" >/dev/null
wait_for_bridge_failure >/dev/null
assert_cmd_vel_zero

ros2 run cockpit_nav2_test_support nav2_fault_control odometry-disable
"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-stale-odom --x 1.0 --y 0 --yaw 0 \
  --config "${config_path}" >/dev/null
wait_for_bridge_failure >/dev/null
assert_cmd_vel_zero
ros2 run cockpit_nav2_test_support nav2_fault_control odometry-enable

"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-odom-recovered --x 1.0 --y 0 --yaw 0 \
  --config "${config_path}" >/dev/null
wait_for_bridge_state NAVIGATION_STATE_SUCCEEDED true >/dev/null
assert_cmd_vel_zero

bt_pid="$(ps -eo pid=,pgid=,comm= | awk -v group="${nav2_pid}" \
  '$2 == group && $3 == "bt_navigator" { print $1; exit }')"
if [[ -z "${bt_pid}" || ! -r "/proc/${bt_pid}/status" ]]; then
  echo "Could not resolve bt_navigator inside Nav2 process group ${nav2_pid}" >&2
  exit 1
fi
kill -TERM "${bt_pid}"
for _ in $(seq 1 100); do
  if ! kill -0 "${bt_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.05
done
if kill -0 "${bt_pid}" >/dev/null 2>&1; then
  echo "bt_navigator did not terminate within its budget" >&2
  exit 1
fi

if "${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-bt-down-first \
  --x 1.2 --y 0 --yaw 0 --config "${config_path}" >/dev/null 2>&1; then
  wait_for_bridge_failure >/dev/null
  assert_cmd_vel_zero
fi
if "${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-bt-down-final \
  --x 1.2 --y 0 --yaw 0 --config "${config_path}" >/dev/null 2>&1; then
  echo "Bridge accepted a goal after bt_navigator removal and DDS expiry" >&2
  exit 1
fi
wait_for_bridge_state NAVIGATION_STATE_DISCONNECTED >/dev/null

stop_nav2
start_nav2
wait_for_bridge_state NAVIGATION_STATE_IDLE >/dev/null
"${bin_dir}/bridge-ctl" --submit --goal-id nav2-minimal-lifecycle-recovered \
  --x 0.6 --y 0 --yaw 0 --config "${config_path}" >/dev/null
wait_for_bridge_state NAVIGATION_STATE_SUCCEEDED true >/dev/null
assert_cmd_vel_zero

"${bin_dir}/cockpit-navigator" --command shutdown --socket "${socket_path}" >/dev/null
wait "${navigator_pid}"
navigator_pid=""
stop_nav2

if rg -q "failed to configure" "${nav2_log}"; then
  echo "Nav2 log contains a lifecycle or navigation failure; see ${nav2_log}" >&2
  exit 1
fi
echo "Official Nav2 minimal Bridge smoke passed; cmd_vel_count=${command_count}"
