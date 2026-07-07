#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

if [[ "${1:-}" == "--offscreen" ]]; then
  export QT_QPA_PLATFORM=offscreen
  shift
fi
if [[ "$#" -ne 0 ]]; then
  echo "usage: $0 [--offscreen]" >&2
  exit 2
fi

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
vehicle_source="${VEHICLE_SOURCE:-mock}"
camera_device="${CAMERA_DEVICE:-/dev/video0}"
camera_auto_start="${CAMERA_AUTO_START:-true}"
camera_required="${CAMERA_REQUIRED:-false}"
vehicle_log="${build_dir}/ui-vehicle-data.log"
gateway_log="${build_dir}/ui-gateway.log"
camera_log="${build_dir}/ui-camera.log"

vehicle_pid=""
gateway_pid=""
camera_pid=""

stop_process() {
  local pid="$1"
  if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
    kill -TERM "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

cleanup() {
  stop_process "${camera_pid}"
  stop_process "${gateway_pid}"
  stop_process "${vehicle_pid}"
}
trap cleanup EXIT INT TERM

for executable in vehicle-data-service cockpit-gateway-service camera-service camera-ctl topic cockpit-ui; do
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

"${bin_dir}/camera-service" --config "${config_path}" >"${camera_log}" 2>&1 &
camera_pid="$!"

gateway_ready=false
camera_ready=false
for _ in $(seq 1 50); do
  if ! kill -0 "${vehicle_pid}" >/dev/null 2>&1; then
    echo "vehicle-data-service exited during startup; see ${vehicle_log}" >&2
    exit 1
  fi
  if ! kill -0 "${gateway_pid}" >/dev/null 2>&1; then
    echo "cockpit-gateway-service exited during startup; see ${gateway_log}" >&2
    exit 1
  fi
  if ! kill -0 "${camera_pid}" >/dev/null 2>&1; then
    echo "camera-service exited during startup; see ${camera_log}" >&2
    exit 1
  fi
  if "${bin_dir}/topic" list --backend grpc --timeout-ms 200 \
      --config "${config_path}" >/dev/null 2>&1; then
    gateway_ready=true
  fi
  if "${bin_dir}/camera-ctl" --status --config "${config_path}" >/dev/null 2>&1; then
    camera_ready=true
  fi
  if [[ "${gateway_ready}" == true && "${camera_ready}" == true ]]; then
    break
  fi
  sleep 0.1
done

if [[ "${gateway_ready}" != true ]]; then
  echo "cockpit-gateway-service did not become ready; see ${gateway_log}" >&2
  exit 1
fi
if [[ "${camera_ready}" != true ]]; then
  echo "camera-service did not become ready; see ${camera_log}" >&2
  exit 1
fi

if [[ "${camera_required}" == true && ! -e "${camera_device}" ]]; then
  echo "required camera device not found: ${camera_device}" >&2
  exit 1
fi

if [[ "${camera_auto_start}" == true && -e "${camera_device}" ]]; then
  if ! "${bin_dir}/camera-ctl" --start --device "${camera_device}" \
      --config "${config_path}" >/dev/null; then
    echo "camera preview did not start; see ${camera_log}" >&2
    exit 1
  fi
  camera_live=false
  for _ in $(seq 1 50); do
    camera_status="$("${bin_dir}/camera-ctl" --status --config "${config_path}" 2>/dev/null || true)"
    if [[ "${camera_status}" == *"state: running"* &&
          "${camera_status}" != *"frames received: 0"* ]]; then
      camera_live=true
      break
    fi
    sleep 0.1
  done
  if [[ "${camera_live}" != true ]]; then
    echo "camera preview started but no live frame arrived; see ${camera_log}" >&2
    exit 1
  fi
fi

echo "cockpit UI connected; service logs: ${vehicle_log}, ${gateway_log}, ${camera_log}"
"${bin_dir}/cockpit-ui" --config "${config_path}"
