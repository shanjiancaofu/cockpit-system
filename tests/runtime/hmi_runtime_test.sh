#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 4 ]]; then
  echo "expected Navigator, cockpit-ctl, module directory and config paths" >&2
  exit 2
fi

navigator_path="$1"
cockpit_ctl_path="$2"
module_dir="$3"
config_path="$4"
run_dir="$(mktemp -d /tmp/cockpit-hmi-test-XXXXXX)"
socket_path="${run_dir}/navigator.sock"
navigator_log="${run_dir}/navigator.log"
navigator_pid=""

find_cockpit_ui_child() {
  local parent_pid="$1"
  pgrep -P "${parent_pid}" -x cockpit-ui 2>/dev/null | head -n 1 || true
}

cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${navigator_path}" --command shutdown --socket "${socket_path}" >/dev/null 2>&1 ||
      kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
  rm -rf "${run_dir}"
}
trap cleanup EXIT

export QT_QPA_PLATFORM=offscreen
(
  cd "${run_dir}"
  exec "${navigator_path}" --config "${config_path}" --module-dir "${module_dir}" \
    --socket "${socket_path}" --mode normal
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

first_hmi_pid=""
first_ui_pid=""
for _ in $(seq 1 100); do
  status="$("${cockpit_ctl_path}" runtime status --socket "${socket_path}" 2>/dev/null || true)"
  first_hmi_pid="$(printf '%s\n' "${status}" | awk '
    $1 == "module=hmi" && $2 == "state=running" { split($3, value, "="); print value[2] }
  ')"
  if [[ -n "${first_hmi_pid}" ]]; then
    first_ui_pid="$(find_cockpit_ui_child "${first_hmi_pid}")"
  fi
  if [[ -n "${first_ui_pid}" && -r "/proc/${first_ui_pid}/cmdline" &&
        "$(tr '\0' ' ' <"/proc/${first_ui_pid}/cmdline")" == *"cockpit-ui"* ]]; then
    break
  fi
  first_ui_pid=""
  sleep 0.1
done
if [[ -z "${first_ui_pid}" ]]; then
  echo "HMI did not start cockpit-ui; see ${navigator_log}" >&2
  exit 1
fi

kill -KILL "${first_ui_pid}"
second_hmi_pid=""
for _ in $(seq 1 100); do
  status="$("${cockpit_ctl_path}" runtime status --socket "${socket_path}" 2>/dev/null || true)"
  second_hmi_pid="$(printf '%s\n' "${status}" | awk -v previous="${first_hmi_pid}" '
    $1 == "module=hmi" && $2 == "state=running" {
      split($3, pid, "=")
      split($5, restarts, "=")
      if (pid[2] != previous && restarts[2] == 1) print pid[2]
    }
  ')"
  if [[ -n "${second_hmi_pid}" ]]; then
    break
  fi
  sleep 0.1
done
if [[ -z "${second_hmi_pid}" ]]; then
  echo "Navigator did not recover HMI after cockpit-ui crashed; see ${navigator_log}" >&2
  exit 1
fi

second_ui_pid=""
for _ in $(seq 1 100); do
  if [[ -n "${second_hmi_pid}" ]]; then
    second_ui_pid="$(find_cockpit_ui_child "${second_hmi_pid}")"
  fi
  if [[ -n "${second_ui_pid}" && -r "/proc/${second_ui_pid}/cmdline" &&
        "$(tr '\0' ' ' <"/proc/${second_ui_pid}/cmdline")" == *"cockpit-ui"* ]]; then
    break
  fi
  second_ui_pid=""
  sleep 0.1
done
if [[ -z "${second_ui_pid}" ]]; then
  echo "restarted HMI did not start cockpit-ui; see ${navigator_log}" >&2
  exit 1
fi

kill -KILL "${second_hmi_pid}"
third_hmi_pid=""
for _ in $(seq 1 100); do
  status="$("${cockpit_ctl_path}" runtime status --socket "${socket_path}" 2>/dev/null || true)"
  third_hmi_pid="$(printf '%s\n' "${status}" | awk -v previous="${second_hmi_pid}" '
    $1 == "module=hmi" && $2 == "state=running" {
      split($3, pid, "=")
      split($5, restarts, "=")
      if (pid[2] != previous && restarts[2] == 2) print pid[2]
    }
  ')"
  if [[ -n "${third_hmi_pid}" && ! -e "/proc/${second_ui_pid}" ]]; then
    break
  fi
  sleep 0.1
done
if [[ -z "${third_hmi_pid}" || -e "/proc/${second_ui_pid}" ]]; then
  echo "HMI module crash left cockpit-ui running or did not recover; see ${navigator_log}" >&2
  exit 1
fi

"${navigator_path}" --command shutdown --socket "${socket_path}" >/dev/null
wait "${navigator_pid}"
navigator_pid=""
echo "HMI runtime lifecycle passed"
