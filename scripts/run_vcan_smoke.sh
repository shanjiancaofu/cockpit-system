#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
interface_name="${CAN_INTERFACE:-vcan0}"
service_log="${build_dir}/vehicle-data-vcan-smoke.log"

bash scripts/setup_vcan.sh "${interface_name}"

service_pid=""
cleanup() {
  if [[ -n "${service_pid}" ]] && kill -0 "${service_pid}" >/dev/null 2>&1; then
    kill "${service_pid}" >/dev/null 2>&1 || true
    wait "${service_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

"${bin_dir}/vehicle-data-service" \
  --config "${config_path}" \
  --source socketcan \
  --samples 3 >"${service_log}" 2>&1 &
service_pid="$!"

sleep 0.2
"${bin_dir}/can-simulator" \
  --config "${config_path}" \
  --backend socketcan \
  --samples 3
wait "${service_pid}"
service_pid=""

cat "${service_log}"
grep -q '"source":"socketcan"' "${service_log}"
