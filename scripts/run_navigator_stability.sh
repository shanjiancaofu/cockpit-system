#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

build_dir="$(cockpit_default_debug_build_dir)"
config_path="${root_dir}/configs/config.yaml"
runtime_dir="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
export COCKPIT_RUNTIME_DIR="${runtime_dir}"
duration_seconds=30
interval_seconds=2
mode="development"
fault="restart"
fault_module="camera_driver"
output_path=""

usage() {
  echo "Usage:"
  echo "  run_navigator_stability.sh [--duration SEC] [--interval SEC]"
  echo "      [--mode normal|development] [--fault none|restart|crash]"
  echo "      [--module NAME] [--output PATH] [--build-dir PATH] [--config PATH]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration) duration_seconds="${2:-}"; shift 2 ;;
    --interval) interval_seconds="${2:-}"; shift 2 ;;
    --mode) mode="${2:-}"; shift 2 ;;
    --fault) fault="${2:-}"; shift 2 ;;
    --module) fault_module="${2:-}"; shift 2 ;;
    --output) output_path="${2:-}"; shift 2 ;;
    --build-dir) build_dir="${2:-}"; shift 2 ;;
    --config) config_path="${2:-}"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! "${duration_seconds}" =~ ^[1-9][0-9]*$ ||
      ! "${interval_seconds}" =~ ^[1-9][0-9]*$ ]]; then
  echo "duration and interval must be positive integers" >&2
  exit 2
fi
if [[ "${mode}" != "normal" && "${mode}" != "development" ]]; then
  echo "mode must be normal or development" >&2
  exit 2
fi
if [[ "${fault}" != "none" && "${fault}" != "restart" && "${fault}" != "crash" ]]; then
  echo "fault must be none, restart or crash" >&2
  exit 2
fi
if [[ ! "${fault_module}" =~ ^[a-z][a-z0-9_]*$ ]]; then
  echo "module name is invalid" >&2
  exit 2
fi

build_dir="$(realpath -m "${build_dir}")"
config_path="$(realpath "${config_path}")"
bin_dir="${build_dir}/bin"
module_dir="${build_dir}/lib/cockpit/modules"
if [[ ! -x "${bin_dir}/cockpit-navigator" || ! -x "${bin_dir}/cockpit-ctl" ||
      ! -d "${module_dir}" ]]; then
  echo "Navigator build artifacts are missing under ${build_dir}" >&2
  exit 1
fi

if [[ -z "${output_path}" ]]; then
  output_path="${runtime_dir}/reports/navigator-stability-report.json"
fi
output_path="$(realpath -m "${output_path}")"
mkdir -p "$(dirname -- "${output_path}")"

if [[ "${mode}" == "development" ]]; then
  expected_modules=(transfer vehicle_driver audio_driver camera_driver agent recording)
else
  expected_modules=(transfer vehicle_driver audio_driver camera_driver agent)
fi
if [[ -x "${bin_dir}/cockpit-ui" ]]; then
  expected_modules+=(hmi)
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
fi
module_expected=false
for expected_module in "${expected_modules[@]}"; do
  if [[ "${fault_module}" == "${expected_module}" ]]; then
    module_expected=true
  fi
done
if [[ "${fault}" != "none" && "${module_expected}" != "true" ]]; then
  echo "fault module ${fault_module} is not active in ${mode} mode" >&2
  exit 2
fi

run_dir="${runtime_dir}/run/stability-${BASHPID}-${RANDOM}"
socket_path="${run_dir}/navigator.sock"
samples_path="${run_dir}/samples.jsonl"
navigator_log="${run_dir}/navigator.log"
mkdir -p "${run_dir}"
: >"${samples_path}"

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
    --socket "${socket_path}" --mode "${mode}"
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

runtime_ready=false
for _ in $(seq 1 100); do
  if "${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" >/dev/null 2>&1; then
    runtime_ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${runtime_ready}" != "true" ]]; then
  echo "Navigator did not become ready; see ${navigator_log}" >&2
  exit 1
fi

sample_count=0
failed_samples=0
collect_sample() {
  local timestamp_ms status runtime_json health_json health_result sample_healthy
  timestamp_ms="$(date +%s%3N)"
  if status="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" 2>/dev/null)"; then
    runtime_json="$(printf '%s\n' "${status}" | awk '
      NR == 1 {
        split($2, pair, "=")
        mode = pair[2]
        next
      }
      {
        delete value
        for (field_index = 1; field_index <= NF; ++field_index) {
          split($field_index, pair, "=")
          value[pair[1]] = pair[2]
        }
        format = "{\"name\":\"%s\",\"state\":\"%s\",\"pid\":%d,"
        format = format "\"exit_code\":%d,\"restart_count\":%d}"
        module[count++] = sprintf(format, value["module"], value["state"], value["pid"], \
                                  value["exit"], value["restarts"])
      }
      END {
        printf "{\"available\":true,\"mode\":\"%s\",\"modules\":[", mode
        for (module_index = 0; module_index < count; ++module_index) {
          printf "%s%s", module_index == 0 ? "" : ",", module[module_index]
        }
        printf "]}"
      }
    ')"
  else
    status=""
    runtime_json='{"available":false,"mode":"unknown","modules":[]}'
  fi

  set +e
  health_json="$("${bin_dir}/cockpit-ctl" health --output json --config "${config_path}" 2>/dev/null)"
  health_result=$?
  set -e
  if [[ -z "${health_json}" ]]; then
    health_json='{"healthy":false,"services":[]}'
  fi

  sample_healthy=true
  for expected_module in "${expected_modules[@]}"; do
    if [[ "${status}" != *"module=${expected_module} state=running"* ]]; then
      sample_healthy=false
    fi
  done
  if [[ "${mode}" == "development" ]]; then
    if [[ ${health_result} -ne 0 ]]; then
      sample_healthy=false
    fi
  elif [[ ! "${health_json}" =~ \"name\":\"gateway\"[^\}]*\"healthy\":true ||
          ! "${health_json}" =~ \"name\":\"audio\"[^\}]*\"healthy\":true ||
          ! "${health_json}" =~ \"name\":\"voice\"[^\}]*\"healthy\":true ||
          ! "${health_json}" =~ \"name\":\"camera\"[^\}]*\"healthy\":true ]]; then
    sample_healthy=false
  fi

  printf '{"timestamp_ms":%s,"healthy":%s,"runtime":%s,"health":%s}\n' \
    "${timestamp_ms}" "${sample_healthy}" "${runtime_json}" "${health_json}" >>"${samples_path}"
  sample_count=$((sample_count + 1))
  if [[ "${sample_healthy}" != "true" ]]; then
    failed_samples=$((failed_samples + 1))
  fi
}

started_at_ms="$(date +%s%3N)"
start_seconds="$(date +%s)"
deadline_seconds=$((start_seconds + duration_seconds))
collect_sample

injection_attempted=false
injection_recovered=true
before_pid=0
after_pid=0
if [[ "${fault}" != "none" ]]; then
  injection_attempted=true
  status_before="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
  before_pid="$(printf '%s\n' "${status_before}" | awk -v target="${fault_module}" '
    $1 == "module=" target { split($3, pair, "="); print pair[2] }
  ')"
  injection_recovered=false
  if [[ "${fault}" == "restart" ]]; then
    "${bin_dir}/cockpit-ctl" runtime restart "${fault_module}" --socket "${socket_path}" \
      >/dev/null
  else
    kill -KILL "${before_pid}"
  fi
  for _ in $(seq 1 100); do
    status_after="$(
      "${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}" 2>/dev/null || true
    )"
    after_pid="$(printf '%s\n' "${status_after}" | awk -v target="${fault_module}" '
      $1 == "module=" target && $2 == "state=running" { split($3, pair, "="); print pair[2] }
    ')"
    if [[ -n "${after_pid}" && "${after_pid}" != "${before_pid}" ]]; then
      injection_recovered=true
      break
    fi
    sleep 0.1
  done
  if [[ "${injection_recovered}" != "true" ]]; then
    after_pid=0
  fi
fi

while [[ "$(date +%s)" -lt "${deadline_seconds}" ]]; do
  sleep "${interval_seconds}"
  collect_sample
done

runtime_alive=true
if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
  runtime_alive=false
fi
report_healthy=true
if [[ ${failed_samples} -ne 0 || "${injection_recovered}" != "true" ||
      "${runtime_alive}" != "true" ]]; then
  report_healthy=false
fi

ended_at_ms="$(date +%s%3N)"
config_sha256="$(sha256sum "${config_path}" | awk '{print $1}')"
project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${build_dir}/CMakeCache.txt")"
git_revision="$(git -C "${root_dir}" rev-parse HEAD 2>/dev/null || echo unknown)"
git_dirty=false
if [[ -n "$(git -C "${root_dir}" status --porcelain)" ]]; then
  git_dirty=true
fi
samples_json="$(paste -sd, "${samples_path}")"
temporary_output="${output_path}.tmp"
{
  printf '{"schema_version":1,"project":"cockpit-system","project_version":"%s",' \
    "${project_version}"
  printf '"git_revision":"%s","git_dirty":%s,"config_sha256":"%s",' \
    "${git_revision}" "${git_dirty}" "${config_sha256}"
  printf '"mode":"%s","started_at_ms":%s,"ended_at_ms":%s,' \
    "${mode}" "${started_at_ms}" "${ended_at_ms}"
  printf '"duration_seconds":%s,"interval_seconds":%s,' \
    "${duration_seconds}" "${interval_seconds}"
  printf '"injection":{"action":"%s","module":"%s","attempted":%s,' \
    "${fault}" "${fault_module}" "${injection_attempted}"
  printf '"before_pid":%s,"after_pid":%s,"recovered":%s},' \
    "${before_pid}" "${after_pid}" "${injection_recovered}"
  printf '"samples":[%s],"summary":{"healthy":%s,"runtime_alive":%s,' \
    "${samples_json}" "${report_healthy}" "${runtime_alive}"
  printf '"sample_count":%s,"failed_samples":%s}}\n' "${sample_count}" "${failed_samples}"
} >"${temporary_output}"
mv "${temporary_output}" "${output_path}"

echo "Navigator stability report: ${output_path}"
if [[ "${report_healthy}" != "true" ]]; then
  echo "Navigator stability check failed; see ${navigator_log}" >&2
  exit 1
fi
