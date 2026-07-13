#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

build_dir="$(realpath -m "${BUILD_DIR:-$(cockpit_default_debug_build_dir)}")"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
bin_dir="${build_dir}/bin"
module_dir="${build_dir}/lib/cockpit/modules"
source_config="$(realpath "${CONFIG_PATH:-${root_dir}/configs/config.yaml}")"
interface_name="${CAN_INTERFACE:-vcan0}"
run_dir="${COCKPIT_RUNTIME_DIR}/run/vcan-${BASHPID}"
config_path="${run_dir}/config.yaml"
socket_path="${run_dir}/navigator.sock"
navigator_log="${run_dir}/navigator.log"

for executable in cockpit-navigator cockpit-ctl can-simulator topic; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}; run bash scripts/build.sh first" >&2
    exit 2
  fi
done
if [[ ! -d "${module_dir}" ]]; then
  echo "missing Navigator modules under ${module_dir}" >&2
  exit 2
fi

bash "${root_dir}/scripts/setup_vcan.sh" "${interface_name}"
mkdir -p "${run_dir}"
awk -v interface_name="${interface_name}" '
  /^    source: / { sub(/source: .*/, "source: socketcan") }
  /^    interface: / { sub(/interface: .*/, "interface: " interface_name) }
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
trap cleanup EXIT

(
  cd "${run_dir}"
  exec "${bin_dir}/cockpit-navigator" --config "${config_path}" --module-dir "${module_dir}" \
    --socket "${socket_path}" --mode cloud
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

runtime_ready=false
for _ in $(seq 1 100); do
  if "${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" >/dev/null 2>&1 &&
      "${bin_dir}/topic" list --backend grpc --timeout-ms 200 \
        --config "${config_path}" >/dev/null 2>&1; then
    runtime_ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${runtime_ready}" != true ]]; then
  echo "Navigator cloud mode did not become ready; see ${navigator_log}" >&2
  exit 1
fi

"${bin_dir}/can-simulator" --config "${config_path}" --backend socketcan --samples 3
vehicle_state="$("${bin_dir}/topic" echo /vehicle/state --backend grpc --count 1 \
  --timeout-ms 2000 --config "${config_path}")"
echo "${vehicle_state}"
if [[ "${vehicle_state}" != *'"source":"socketcan"'* ]]; then
  echo "Navigator did not publish a SocketCAN vehicle state; see ${navigator_log}" >&2
  exit 1
fi

echo "Navigator vcan smoke passed; log: ${navigator_log}"
