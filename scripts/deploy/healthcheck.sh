#!/usr/bin/env bash
set -euo pipefail

install_root="${COCKPIT_ROOT:-/cockpit-system}"
socket_path="${COCKPIT_SOCKET:-${install_root}/run/navigator.sock}"
ctl="${install_root}/current/bin/cockpit-ctl"

mode_output="$("${ctl}" runtime mode --socket "${socket_path}")"
mode="${mode_output#OK mode=}"
if [[ "${mode}" != "normal" && "${mode}" != "development" && "${mode}" != "cloud" ]]; then
  echo "unable to determine Navigator mode: ${mode_output}" >&2
  exit 1
fi

runtime_status="$("${ctl}" runtime status --socket "${socket_path}")"
case "${mode}" in
  normal)
    expected_modules=(transfer vehicle_driver audio_driver camera_driver agent)
    ;;
  development)
    expected_modules=(transfer vehicle_driver audio_driver camera_driver agent recording)
    ;;
  cloud)
    expected_modules=(transfer vehicle_driver carupload)
    ;;
esac
if [[ ( "${mode}" == "normal" || "${mode}" == "development" ) &&
      -x "${install_root}/current/bin/cockpit-ui" ]]; then
  expected_modules+=(hmi)
fi
for module in "${expected_modules[@]}"; do
  if [[ "${runtime_status}" != *"module=${module} state=running"* ]]; then
    echo "Navigator module is not running: ${module}" >&2
    exit 1
  fi
done

exec "${ctl}" health --mode "${mode}" --config "${install_root}/config/config.yaml"
