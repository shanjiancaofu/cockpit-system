#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

if [[ "${1:-}" == "--offscreen" ]]; then
  export QT_QPA_PLATFORM=offscreen
  shift
fi
if [[ "$#" -ne 0 ]]; then
  echo "usage: $0 [--offscreen]" >&2
  exit 2
fi

build_dir="$(realpath -m "${BUILD_DIR:-$(cockpit_default_debug_build_dir)}")"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
bin_dir="${build_dir}/bin"
module_dir="${build_dir}/lib/cockpit/modules"
source_config="$(realpath "${CONFIG_PATH:-${root_dir}/configs/config.yaml}")"
vehicle_source="${VEHICLE_SOURCE:-mock}"
camera_device="${CAMERA_DEVICE:-/dev/video0}"
camera_auto_start="${CAMERA_AUTO_START:-true}"
camera_required="${CAMERA_REQUIRED:-false}"
run_dir="${COCKPIT_RUNTIME_DIR}/run/ui-${BASHPID}"
config_path="${run_dir}/config.yaml"
socket_path="${run_dir}/navigator.sock"
navigator_log="${run_dir}/navigator.log"

for executable in cockpit-navigator cockpit-ctl camera-ctl topic cockpit-ui; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}" >&2
    echo "build with: bash scripts/build.sh" >&2
    echo "            cmake --build ${build_dir}" >&2
    exit 2
  fi
done
if [[ ! -d "${module_dir}" ]]; then
  echo "missing Navigator modules under ${module_dir}" >&2
  exit 2
fi

mkdir -p "${run_dir}"
awk -v source="${vehicle_source}" '
  /^    source: / { sub(/source: .*/, "source: " source) }
  { print }
' "${source_config}" >"${config_path}"

navigator_pid=""
cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${bin_dir}/cockpit-navigator" --command shutdown --socket "${socket_path}" \
      >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

(
  cd "${run_dir}"
  exec "${bin_dir}/cockpit-navigator" --config "${config_path}" --module-dir "${module_dir}" \
    --socket "${socket_path}" --mode ui
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

runtime_ready=false
for _ in $(seq 1 100); do
  if "${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" >/dev/null 2>&1 &&
      "${bin_dir}/topic" list --backend grpc --timeout-ms 200 \
        --config "${config_path}" >/dev/null 2>&1 &&
      "${bin_dir}/camera-ctl" --status --config "${config_path}" >/dev/null 2>&1; then
    runtime_ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${runtime_ready}" != true ]]; then
  echo "Navigator ui mode did not become ready; see ${navigator_log}" >&2
  exit 1
fi
runtime_status="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
if [[ "${runtime_status}" != *"module=hmi state=running"* ]]; then
  echo "Navigator did not start HMI; see ${navigator_log}" >&2
  exit 1
fi

camera_available=false
if [[ "${camera_device}" == nvargus://* || -e "${camera_device}" ]]; then
  camera_available=true
fi
if [[ "${camera_required}" == true && "${camera_available}" != true ]]; then
  echo "required camera device not found: ${camera_device}" >&2
  exit 1
fi

if [[ "${camera_auto_start}" == true && "${camera_available}" == true ]]; then
  if ! "${bin_dir}/camera-ctl" --start --device "${camera_device}" \
      --config "${config_path}" >/dev/null; then
    echo "camera preview did not start; see ${navigator_log}" >&2
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
    echo "camera preview started but no live frame arrived; see ${navigator_log}" >&2
    exit 1
  fi
fi

echo "cockpit UI is managed by Navigator; press Ctrl+C to stop; log: ${navigator_log}"
wait "${navigator_pid}"
navigator_pid=""
