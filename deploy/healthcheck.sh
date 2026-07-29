#!/usr/bin/env bash
set -euo pipefail

install_root="${COCKPIT_ROOT:-/cockpit-system}"
socket_path="${COCKPIT_SOCKET:-${install_root}/run/navigator.sock}"
ctl="${install_root}/current/bin/cockpit-ctl"

mode_output="$("${ctl}" runtime mode --socket "${socket_path}")"
mode="${mode_output#OK mode=}"
if [[ "${mode}" != "normal" && "${mode}" != "development" && "${mode}" != "ui" &&
      "${mode}" != "cloud" ]]; then
  echo "unable to determine Navigator mode: ${mode_output}" >&2
  exit 1
fi

runtime_status="$("${ctl}" runtime status --socket "${socket_path}")"
expected_release="$(readlink -f "${install_root}/current")"
expected_version="$(tr -d '\r\n' <"${install_root}/current/manifest/VERSION")"
runtime_header="${runtime_status%%$'\n'*}"
runtime_version="$(sed -n 's/.* version=\([^ ]*\\).*/\1/p' <<<"${runtime_header}")"
navigator_executable="$(sed -n 's/.* executable=\([^ ]*\\).*/\1/p' <<<"${runtime_header}")"
if [[ "${runtime_version}" != "${expected_version}" ]]; then
  echo "Navigator version mismatch: expected ${expected_version}, got ${runtime_version}" >&2
  exit 1
fi
if [[ -z "${navigator_executable}" ||
      "$(readlink -f "${navigator_executable}")" != "${expected_release}/bin/cockpit-navigator" ]]; then
  echo "Navigator executable is not from the active release: ${navigator_executable}" >&2
  exit 1
fi
case "${mode}" in
  normal)
    expected_modules=(transfer vehicle_driver audio_driver camera_driver agent)
    ;;
  development)
    expected_modules=(transfer vehicle_driver audio_driver camera_driver agent recording)
    ;;
  ui)
    expected_modules=(transfer vehicle_driver audio_driver camera_driver agent hmi)
    ;;
  cloud)
    expected_modules=(transfer vehicle_driver carupload)
    ;;
esac
for module in "${expected_modules[@]}"; do
  module_status="$(grep -m1 "^module=${module} " <<<"${runtime_status}" || true)"
  if [[ "${module_status}" != *"state=running"* ]]; then
    echo "Navigator module is not running: ${module}" >&2
    exit 1
  fi
  module_pid="$(sed -n 's/.* pid=\([0-9][0-9]*\).*/\1/p' <<<"${module_status}")"
  module_executable="$(readlink -f "/proc/${module_pid}/exe" 2>/dev/null || true)"
  if [[ -z "${module_pid}" ||
        "${module_executable}" != "${expected_release}/bin/cockpit-navigator" ]]; then
    echo "Navigator module is not from the active release: ${module}" >&2
    exit 1
  fi
done

exec "${ctl}" health --mode "${mode}" --config "${install_root}/config/config.yaml"
