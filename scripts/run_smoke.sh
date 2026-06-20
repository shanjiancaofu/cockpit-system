#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
vehicle_log="${build_dir}/vehicle-data-grpc-smoke.log"

vehicle_pid=""
cleanup() {
  if [[ -n "${vehicle_pid}" ]] && kill -0 "${vehicle_pid}" >/dev/null 2>&1; then
    kill "${vehicle_pid}" >/dev/null 2>&1 || true
    wait "${vehicle_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

"${bin_dir}/can-simulator" --config "${config_path}" --samples 3
"${bin_dir}/vehicle-data-service" --config "${config_path}" --forever >"${vehicle_log}" 2>&1 &
vehicle_pid="$!"
sleep 0.2
"${bin_dir}/cockpit-gateway-service" --config "${config_path}" --samples 3
kill "${vehicle_pid}"
wait "${vehicle_pid}" || true
vehicle_pid=""
cat "${vehicle_log}"
"${bin_dir}/cloud-uplink-service" --config "${config_path}" --once
"${bin_dir}/topic" pub /dev/smoke '{"ok":true,"source":"run_smoke"}' --repeat 3 --rate-ms 20 --config "${config_path}"
"${bin_dir}/topic" list --config "${config_path}"
"${bin_dir}/topic" echo /dev/smoke --tail 1 --config "${config_path}"
"${bin_dir}/topic" hz /dev/smoke --window 3 --config "${config_path}"
