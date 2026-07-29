#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

debug_build_dir="${DEBUG_BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
release_build_dir="${RELEASE_BUILD_DIR:-$(cockpit_default_release_build_dir)}"
output_dir="$(cockpit_output_dir)"
report_path="${REPORT_PATH:-${output_dir}/runtime/reports/wsl-matrix.json}"
work_parent="${output_dir}/runtime/run"
mkdir -p "${work_parent}"
work_dir="$(mktemp -d "${work_parent}/wsl-matrix-XXXXXX")"

configuration_status="not_run"
deployment_status="not_run"
ota_recovery_status="not_run"
diagnostic_snapshot_status="not_run"
gateway_status="not_run"
recording_status="not_run"
queue_status="not_run"
shared_memory_status="not_run"
navigator_pid=""

finish() {
  local result=$?
  set +e
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${install_root:-}/current/bin/cockpit-navigator" --command shutdown \
      --socket "${socket_path:-}" >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1
    wait "${navigator_pid}" >/dev/null 2>&1
  fi
  rm -rf "${work_dir}"

  local overall="failed"
  if [[ "${result}" -eq 0 ]]; then
    overall="passed"
  fi
  mkdir -p "$(dirname -- "${report_path}")"
  cat >"${report_path}" <<EOF
{
  "schema_version": 1,
  "generated_at_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "git_revision": "$(git -C "${root_dir}" rev-parse HEAD)",
  "overall": "${overall}",
  "cases": {
    "configuration_boundary": "${configuration_status}",
    "package_install_healthcheck_rollback": "${deployment_status}",
    "ota_startup_recovery": "${ota_recovery_status}",
    "diagnostic_snapshot": "${diagnostic_snapshot_status}",
    "gateway_stream_loss": "${gateway_status}",
    "recording_unavailable": "${recording_status}",
    "queue_full": "${queue_status}",
    "shared_memory_writer_restart": "${shared_memory_status}"
  }
}
EOF
  echo "WSL matrix ${overall}; report: ${report_path}"
  trap - EXIT
  exit "${result}"
}
trap finish EXIT

debug_package_info="${debug_build_dir}/package-info.env"
release_package_info="${release_build_dir}/package-info.env"
if [[ ! -f "${debug_package_info}" ]]; then
  echo "Debug build directory not found: ${debug_build_dir}" >&2
  exit 2
fi
if [[ ! -f "${release_package_info}" ]]; then
  echo "Release build directory not found: ${release_build_dir}" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${debug_package_info}"
debug_compiler_id="${COCKPIT_COMPILER_ID}"
# shellcheck disable=SC1090
source "${release_package_info}"
release_compiler_id="${COCKPIT_COMPILER_ID}"
if [[ "${debug_compiler_id}" != "GNU" ]]; then
  echo "WSL matrix requires a GCC Debug build; found '${debug_compiler_id:-unknown}'" >&2
  exit 2
fi
if [[ "${release_compiler_id}" != "GNU" ]]; then
  echo "WSL matrix requires a GCC Release build; found '${release_compiler_id:-unknown}'" >&2
  exit 2
fi

configuration_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure -R '^system_config_test$'
configuration_status="passed"

queue_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure \
  -R '^(event_queue_test|message_bus_test|async_voice_response_sink_test)$'
queue_status="passed"

shared_memory_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure -R '^shared_frame_buffer_test$'
shared_memory_status="passed"

ota_recovery_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure -R '^safe_ota_test$'
ota_recovery_status="passed"

diagnostic_snapshot_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure -R '^diagnostic_snapshot_test$'
diagnostic_snapshot_status="passed"

gateway_status="failed"
ctest --test-dir "${debug_build_dir}" --output-on-failure -R '^gateway_grpc_service_test$'

deployment_status="failed"
stage_dir="${work_dir}/stage"
dist_dir="${work_dir}/dist"
version_a="0.1.0-matrix-a"
version_b="0.1.0-matrix-b"
BUILD_DIR="${release_build_dir}" STAGE_DIR="${stage_dir}" DIST_DIR="${dist_dir}" \
  VERSION="${version_a}" bash "${root_dir}/scripts/package.sh"
BUILD_DIR="${release_build_dir}" STAGE_DIR="${stage_dir}" DIST_DIR="${dist_dir}" \
  VERSION="${version_b}" bash "${root_dir}/scripts/package.sh"

package_a="$(find "${stage_dir}" -mindepth 1 -maxdepth 1 -type d \
  -name "cockpit-system-${version_a}-*" -print -quit)"
package_b="$(find "${stage_dir}" -mindepth 1 -maxdepth 1 -type d \
  -name "cockpit-system-${version_b}-*" -print -quit)"
if [[ -z "${package_a}" || -z "${package_b}" ]]; then
  echo "packaged release directory was not created" >&2
  exit 1
fi

install_root="${work_dir}/install"
COCKPIT_ROOT="${install_root}" INSTALL_SYSTEMD=false bash "${package_a}/deploy/install.sh"
if [[ "$(readlink "${install_root}/current")" != "releases/${version_a}" ]]; then
  echo "first package was not activated" >&2
  exit 1
fi
sed -i 's/vehicle_id: car_001/vehicle_id: matrix_vehicle/' \
  "${install_root}/config/config.yaml"

COCKPIT_ROOT="${install_root}" INSTALL_SYSTEMD=false bash "${package_b}/deploy/install.sh"
if [[ "$(readlink "${install_root}/current")" != "releases/${version_b}" ]] ||
   ! grep -q 'vehicle_id: matrix_vehicle' "${install_root}/config/config.yaml" ||
   [[ ! -f "${install_root}/config/config.yaml.new" ]]; then
  echo "second package activation did not preserve the active config" >&2
  exit 1
fi

tampered_package="${work_dir}/tampered-package"
cp -a "${package_b}" "${tampered_package}"
printf 'tampered\n' >>"${tampered_package}/release/bin/cockpit-ctl"
set +e
COCKPIT_ROOT="${install_root}" INSTALL_SYSTEMD=false \
  bash "${tampered_package}/deploy/install.sh" >/dev/null 2>&1
tampered_result=$?
set -e
if [[ "${tampered_result}" -eq 0 ]]; then
  echo "tampered package was installed" >&2
  exit 1
fi

socket_path="${install_root}/run/navigator.sock"
navigator_log="${work_dir}/navigator.log"
(
  cd "${install_root}"
  exec env COCKPIT_RUNTIME_DIR="${install_root}" QT_QPA_PLATFORM=offscreen \
    "${install_root}/current/bin/cockpit-navigator" \
    --config "${install_root}/config/config.yaml" \
    --module-dir "${install_root}/current/lib/cockpit/modules" \
    --socket "${socket_path}" --mode normal
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

runtime_ready=false
for _ in $(seq 1 100); do
  if "${install_root}/current/bin/cockpit-ctl" runtime status \
      --socket "${socket_path}" >/dev/null 2>&1; then
    runtime_ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${runtime_ready}" != "true" ]]; then
  echo "installed Navigator did not become ready; see ${navigator_log}" >&2
  exit 1
fi

topic_info=""
for _ in $(seq 1 50); do
  topic_info="$("${install_root}/current/bin/topic" info /vehicle/state --backend grpc \
    --config "${install_root}/config/config.yaml")"
  if [[ "${topic_info}" == *"availability: available"* ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${topic_info}" != *"availability: available"* ]]; then
  echo "installed gateway did not receive vehicle state" >&2
  exit 1
fi

COCKPIT_ROOT="${install_root}" bash "${package_b}/deploy/healthcheck.sh"

recording_status="failed"
set +e
"${install_root}/current/bin/recording-ctl" --status \
  --config "${install_root}/config/config.yaml" >/dev/null 2>&1
recording_result=$?
set -e
if [[ "${recording_result}" -eq 0 ]]; then
  echo "recording unexpectedly remained available in normal mode" >&2
  exit 1
fi
recording_status="passed"

"${install_root}/current/bin/cockpit-ctl" runtime stop vehicle_driver \
  --socket "${socket_path}" >/dev/null
for _ in $(seq 1 40); do
  topic_info="$("${install_root}/current/bin/topic" info /vehicle/state --backend grpc \
    --config "${install_root}/config/config.yaml")"
  if [[ "${topic_info}" == *"availability: stale"* ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${topic_info}" != *"availability: stale"* ]]; then
  echo "gateway did not report stale data after vehicle stream loss" >&2
  exit 1
fi

"${install_root}/current/bin/cockpit-ctl" runtime start vehicle_driver \
  --socket "${socket_path}" >/dev/null
for _ in $(seq 1 50); do
  topic_info="$("${install_root}/current/bin/topic" info /vehicle/state --backend grpc \
    --config "${install_root}/config/config.yaml")"
  if [[ "${topic_info}" == *"availability: available"* ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${topic_info}" != *"availability: available"* ]]; then
  echo "gateway did not recover after vehicle driver restart" >&2
  exit 1
fi
gateway_status="passed"

COCKPIT_ROOT="${install_root}" bash "${package_b}/deploy/healthcheck.sh"
"${install_root}/current/bin/cockpit-navigator" --command shutdown \
  --socket "${socket_path}" >/dev/null
wait "${navigator_pid}"
navigator_pid=""

COCKPIT_ROOT="${install_root}" bash "${package_b}/deploy/rollback.sh" "${version_a}"
if [[ "$(readlink "${install_root}/current")" != "releases/${version_a}" ]]; then
  echo "rollback did not activate the previous release" >&2
  exit 1
fi
deployment_status="passed"
