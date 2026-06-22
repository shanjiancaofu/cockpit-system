#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--offscreen" ]]; then
  export QT_QPA_PLATFORM=offscreen
  shift
fi
if [[ "$#" -ne 0 ]]; then
  echo "usage: $0 [--offscreen]" >&2
  exit 2
fi

build_dir="${BUILD_DIR:-build}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
vehicle_source="${VEHICLE_SOURCE:-mock}"
vehicle_log="${build_dir}/ui-vehicle-data.log"
gateway_log="${build_dir}/ui-gateway.log"

vehicle_pid=""
gateway_pid=""

stop_process() {
  local pid="$1"
  if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
    kill -TERM "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

cleanup() {
  stop_process "${gateway_pid}"
  stop_process "${vehicle_pid}"
}
trap cleanup EXIT INT TERM

for executable in vehicle-data-service cockpit-gateway-service topic cockpit-ui; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}" >&2
    echo "build with: cmake -S . -B ${build_dir} -G Ninja -DBUILD_COCKPIT_UI=ON" >&2
    echo "            cmake --build ${build_dir}" >&2
    exit 2
  fi
done

mkdir -p "${build_dir}"
"${bin_dir}/vehicle-data-service" --config "${config_path}" \
  --source "${vehicle_source}" --forever >"${vehicle_log}" 2>&1 &
vehicle_pid="$!"

"${bin_dir}/cockpit-gateway-service" --config "${config_path}" \
  >"${gateway_log}" 2>&1 &
gateway_pid="$!"

gateway_ready=false
for _ in $(seq 1 50); do
  if ! kill -0 "${vehicle_pid}" >/dev/null 2>&1; then
    echo "vehicle-data-service exited during startup; see ${vehicle_log}" >&2
    exit 1
  fi
  if ! kill -0 "${gateway_pid}" >/dev/null 2>&1; then
    echo "cockpit-gateway-service exited during startup; see ${gateway_log}" >&2
    exit 1
  fi
  if "${bin_dir}/topic" list --backend grpc --timeout-ms 200 \
      --config "${config_path}" >/dev/null 2>&1; then
    gateway_ready=true
    break
  fi
  sleep 0.1
done

if [[ "${gateway_ready}" != true ]]; then
  echo "cockpit-gateway-service did not become ready; see ${gateway_log}" >&2
  exit 1
fi

echo "cockpit UI connected; service logs: ${vehicle_log}, ${gateway_log}"
"${bin_dir}/cockpit-ui" --config "${config_path}"
