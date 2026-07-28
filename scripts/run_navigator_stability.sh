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
fault_count=1
output_path=""
snapshot_max_count=10
snapshot_max_total_bytes=104857600

usage() {
  echo "Usage:"
  echo "  run_navigator_stability.sh [--duration SEC] [--interval SEC]"
  echo "      [--mode normal|development] [--fault none|restart|crash]"
  echo "      [--module NAME] [--fault-count N] [--output PATH]"
  echo "      [--build-dir PATH] [--config PATH]"
  echo "      [--snapshot-max-count N] [--snapshot-max-total-bytes N]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration) duration_seconds="${2:-}"; shift 2 ;;
    --interval) interval_seconds="${2:-}"; shift 2 ;;
    --mode) mode="${2:-}"; shift 2 ;;
    --fault) fault="${2:-}"; shift 2 ;;
    --module) fault_module="${2:-}"; shift 2 ;;
    --fault-count) fault_count="${2:-}"; shift 2 ;;
    --output) output_path="${2:-}"; shift 2 ;;
    --build-dir) build_dir="${2:-}"; shift 2 ;;
    --config) config_path="${2:-}"; shift 2 ;;
    --snapshot-max-count) snapshot_max_count="${2:-}"; shift 2 ;;
    --snapshot-max-total-bytes) snapshot_max_total_bytes="${2:-}"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! "${duration_seconds}" =~ ^[1-9][0-9]*$ ||
      ! "${interval_seconds}" =~ ^[1-9][0-9]*$ ||
      ! "${fault_count}" =~ ^[1-9][0-9]*$ ||
      ! "${snapshot_max_count}" =~ ^[1-9][0-9]*$ ||
      ! "${snapshot_max_total_bytes}" =~ ^[1-9][0-9]*$ ]]; then
  echo "duration, interval, fault count and snapshot limits must be positive integers" >&2
  exit 2
fi
duration_seconds=$((10#${duration_seconds}))
interval_seconds=$((10#${interval_seconds}))
fault_count=$((10#${fault_count}))
snapshot_max_count=$((10#${snapshot_max_count}))
snapshot_max_total_bytes=$((10#${snapshot_max_total_bytes}))
if ((interval_seconds > duration_seconds)); then
  echo "interval must not exceed duration" >&2
  exit 2
fi
if ((fault_count > 100 || snapshot_max_count > 100 ||
      snapshot_max_total_bytes > 1073741824)); then
  echo "fault and snapshot max count must be <= 100; total bytes must be <= 1073741824" >&2
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
injections_path="${run_dir}/injections.jsonl"
navigator_log="${run_dir}/navigator.log"
mkdir -p "${run_dir}"
: >"${samples_path}"
: >"${injections_path}"

navigator_pid=""
started_at_ms="$(date +%s%3N)"
sample_count=0
failed_samples=0
injection_attempted=false
injection_recovered=true
injection_requested_count=0
injection_attempted_count=0
injection_recovered_count=0
runtime_alive=false
report_healthy=false
snapshot_attempted=false
snapshot_available=false
snapshot_reason=""
snapshot_path=""
report_written=false
resource_unavailable_samples=0
resource_initial_json='{"timestamp_ms":0,"rss_kib":0,"threads":0,"fd_count":0,"storage":{}}'
resource_final_json="${resource_initial_json}"
resource_peak_rss_kib=0
resource_peak_threads=0
resource_peak_fd_count=0
resource_peak_logs_bytes=0
resource_peak_recordings_bytes=0
resource_peak_reports_bytes=0
resource_peak_snapshots_bytes=0

read_process_resources() {
  local pid="$1"
  local status_path="/proc/${pid}/status"
  local fd_path="/proc/${pid}/fd"
  local metrics rss_kib threads fd_count
  if [[ ! "${pid}" =~ ^[1-9][0-9]*$ || ! -r "${status_path}" || ! -d "${fd_path}" ]]; then
    printf 'false 0 0 0\n'
    return
  fi
  metrics="$(awk '
    /^VmRSS:/ { rss_kib = $2 }
    /^Threads:/ { threads = $2 }
    END { printf "%d %d", rss_kib, threads }
  ' "${status_path}" 2>/dev/null || true)"
  read -r rss_kib threads <<<"${metrics}"
  if ! fd_count="$(
    find "${fd_path}" -mindepth 1 -maxdepth 1 -printf x 2>/dev/null | wc -c
  )"; then
    printf 'false 0 0 0\n'
    return
  fi
  if [[ -z "${rss_kib}" || -z "${threads}" ]]; then
    printf 'false 0 0 0\n'
    return
  fi
  printf 'true %s %s %s\n' "${rss_kib}" "${threads}" "${fd_count}"
}

directory_bytes() {
  local path="$1"
  if [[ ! -e "${path}" ]]; then
    printf '0\n'
    return
  fi
  du -sb -- "${path}" 2>/dev/null | awk '{print $1}'
}

capture_failure_snapshot() {
  if [[ "${snapshot_attempted}" == "true" ]]; then
    return
  fi
  snapshot_attempted=true
  snapshot_reason="$1"
  snapshot_path="${runtime_dir}/data/diagnostics/snapshot-$(date +%s%3N)"
  if "${bin_dir}/cockpit-ctl" snapshot --config "${config_path}" --socket "${socket_path}" \
      --directory "${snapshot_path}" --max-snapshots "${snapshot_max_count}" \
      --max-total-bytes "${snapshot_max_total_bytes}" >/dev/null 2>&1; then
    snapshot_available=true
  fi
}

write_report() {
  local ended_at_ms config_sha256 project_version git_revision git_dirty samples_json
  local injections_json
  ended_at_ms="$(date +%s%3N)"
  config_sha256="$(sha256sum "${config_path}" | awk '{print $1}')"
  # shellcheck disable=SC1090
  source "${build_dir}/package-info.env"
  project_version="${COCKPIT_VERSION}"
  git_revision="$(git -C "${root_dir}" rev-parse HEAD 2>/dev/null || echo unknown)"
  git_dirty=false
  if [[ -n "$(git -C "${root_dir}" status --porcelain)" ]]; then
    git_dirty=true
  fi
  samples_json="$(paste -sd, "${samples_path}")"
  injections_json="$(paste -sd, "${injections_path}")"
  local temporary_output="${output_path}.tmp"
  {
    printf '{"schema_version":3,"project":"cockpit-system","project_version":"%s",' \
      "${project_version}"
    printf '"git_revision":"%s","git_dirty":%s,"config_sha256":"%s",' \
      "${git_revision}" "${git_dirty}" "${config_sha256}"
    printf '"mode":"%s","started_at_ms":%s,"ended_at_ms":%s,' \
      "${mode}" "${started_at_ms}" "${ended_at_ms}"
    printf '"duration_seconds":%s,"interval_seconds":%s,"navigator_log":"%s",' \
      "${duration_seconds}" "${interval_seconds}" "${navigator_log}"
    printf '"injection":{"action":"%s","module":"%s","requested_count":%s,' \
      "${fault}" "${fault_module}" "${injection_requested_count}"
    printf '"attempted":%s,"attempted_count":%s,"recovered_count":%s,' \
      "${injection_attempted}" "${injection_attempted_count}" "${injection_recovered_count}"
    printf '"recovered":%s,"events":[%s]},' "${injection_recovered}" "${injections_json}"
    printf '"diagnostic_snapshot":{"attempted":%s,"available":%s,' \
      "${snapshot_attempted}" "${snapshot_available}"
    printf '"reason":"%s","path":"%s"},' "${snapshot_reason}" "${snapshot_path}"
    printf '"resources":{"initial":%s,' "${resource_initial_json}"
    printf '"peak":{"rss_kib":%s,"threads":%s,"fd_count":%s,' \
      "${resource_peak_rss_kib}" "${resource_peak_threads}" "${resource_peak_fd_count}"
    printf '"storage":{"logs_bytes":%s,"recordings_bytes":%s,"reports_bytes":%s,' \
      "${resource_peak_logs_bytes}" "${resource_peak_recordings_bytes}" \
      "${resource_peak_reports_bytes}"
    printf '"snapshots_bytes":%s}},"final":%s,"unavailable_samples":%s},' \
      "${resource_peak_snapshots_bytes}" "${resource_final_json}" \
      "${resource_unavailable_samples}"
    printf '"samples":[%s],"summary":{"healthy":%s,"runtime_alive":%s,' \
      "${samples_json}" "${report_healthy}" "${runtime_alive}"
    printf '"sample_count":%s,"failed_samples":%s}}\n' "${sample_count}" "${failed_samples}"
  } >"${temporary_output}"
  mv "${temporary_output}" "${output_path}"
  report_written=true
}

cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${bin_dir}/cockpit-navigator" --command shutdown --socket "${socket_path}" \
      >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
}

finish() {
  local result=$?
  set +e
  if [[ ${result} -ne 0 && "${report_written}" != "true" ]]; then
    if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
      runtime_alive=true
    fi
    capture_failure_snapshot "script_failed"
    write_report
  fi
  cleanup
  trap - EXIT
  exit "${result}"
}
trap finish EXIT

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
  if kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    runtime_alive=true
  fi
  capture_failure_snapshot "runtime_not_ready"
  write_report
  echo "Navigator did not become ready; see ${navigator_log}" >&2
  exit 1
fi

collect_sample() {
  local timestamp_ms status runtime_json health_json health_result sample_healthy
  local resource_available process_pid process_state status_field resource_summary_json
  local process_available process_rss_kib process_threads process_fd_count
  local total_rss_kib=0 total_threads=0 total_fd_count=0
  local navigator_available navigator_rss_kib navigator_threads navigator_fd_count
  local module_count=0 available_module_count=0
  local module_rss_kib=0 module_threads=0 module_fd_count=0
  local logs_bytes recordings_bytes reports_bytes snapshots_bytes
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

  read -r process_available process_rss_kib process_threads process_fd_count \
    < <(read_process_resources "${navigator_pid}")
  navigator_available="${process_available}"
  navigator_rss_kib="${process_rss_kib}"
  navigator_threads="${process_threads}"
  navigator_fd_count="${process_fd_count}"
  resource_available="${process_available}"
  if [[ "${process_available}" == "true" ]]; then
    total_rss_kib=$((total_rss_kib + process_rss_kib))
    total_threads=$((total_threads + process_threads))
    total_fd_count=$((total_fd_count + process_fd_count))
  fi
  while IFS= read -r status_line; do
    if [[ "${status_line}" != module=* ]]; then
      continue
    fi
    process_pid=0
    process_state=""
    for status_field in ${status_line}; do
      case "${status_field}" in
        state=*) process_state="${status_field#state=}" ;;
        pid=*) process_pid="${status_field#pid=}" ;;
      esac
    done
    if [[ "${process_state}" != "running" ]]; then
      continue
    fi
    module_count=$((module_count + 1))
    read -r process_available process_rss_kib process_threads process_fd_count \
      < <(read_process_resources "${process_pid}")
    if [[ "${process_available}" == "true" ]]; then
      available_module_count=$((available_module_count + 1))
      module_rss_kib=$((module_rss_kib + process_rss_kib))
      module_threads=$((module_threads + process_threads))
      module_fd_count=$((module_fd_count + process_fd_count))
      total_rss_kib=$((total_rss_kib + process_rss_kib))
      total_threads=$((total_threads + process_threads))
      total_fd_count=$((total_fd_count + process_fd_count))
    else
      resource_available=false
    fi
  done <<<"${status}"
  logs_bytes="$(directory_bytes "${runtime_dir}/logs")"
  recordings_bytes="$(directory_bytes "${runtime_dir}/data/recordings")"
  reports_bytes="$(directory_bytes "${runtime_dir}/reports")"
  snapshots_bytes="$(directory_bytes "${runtime_dir}/data/diagnostics")"
  resource_summary_json="{\"timestamp_ms\":${timestamp_ms},\"rss_kib\":${total_rss_kib},"
  resource_summary_json+="\"threads\":${total_threads},\"fd_count\":${total_fd_count},"
  resource_summary_json+="\"storage\":{\"logs_bytes\":${logs_bytes},"
  resource_summary_json+="\"recordings_bytes\":${recordings_bytes},"
  resource_summary_json+="\"reports_bytes\":${reports_bytes},"
  resource_summary_json+="\"snapshots_bytes\":${snapshots_bytes}}}"

  if [[ ${sample_count} -eq 0 ]]; then
    resource_initial_json="${resource_summary_json}"
  fi
  ((total_rss_kib > resource_peak_rss_kib)) && resource_peak_rss_kib="${total_rss_kib}"
  ((total_threads > resource_peak_threads)) && resource_peak_threads="${total_threads}"
  ((total_fd_count > resource_peak_fd_count)) && resource_peak_fd_count="${total_fd_count}"
  ((logs_bytes > resource_peak_logs_bytes)) && resource_peak_logs_bytes="${logs_bytes}"
  ((recordings_bytes > resource_peak_recordings_bytes)) && \
    resource_peak_recordings_bytes="${recordings_bytes}"
  ((reports_bytes > resource_peak_reports_bytes)) && \
    resource_peak_reports_bytes="${reports_bytes}"
  ((snapshots_bytes > resource_peak_snapshots_bytes)) && \
    resource_peak_snapshots_bytes="${snapshots_bytes}"
  resource_final_json="${resource_summary_json}"
  if [[ "${resource_available}" != "true" ]]; then
    resource_unavailable_samples=$((resource_unavailable_samples + 1))
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

  printf '{"timestamp_ms":%s,"healthy":%s,"runtime":%s,"health":%s,' \
    "${timestamp_ms}" "${sample_healthy}" "${runtime_json}" "${health_json}" \
    >>"${samples_path}"
  printf '"resources":{"available":%s,"navigator":{"pid":%s,"available":%s,' \
    "${resource_available}" "${navigator_pid}" "${navigator_available}" >>"${samples_path}"
  printf '"rss_kib":%s,"threads":%s,"fd_count":%s},' \
    "${navigator_rss_kib}" "${navigator_threads}" "${navigator_fd_count}" >>"${samples_path}"
  printf '"modules":{"process_count":%s,"available_process_count":%s,' \
    "${module_count}" "${available_module_count}" >>"${samples_path}"
  printf '"rss_kib":%s,"threads":%s,"fd_count":%s},"total":' \
    "${module_rss_kib}" "${module_threads}" "${module_fd_count}" >>"${samples_path}"
  printf '%s}}\n' "${resource_summary_json}" >>"${samples_path}"
  sample_count=$((sample_count + 1))
  if [[ "${sample_healthy}" != "true" ]]; then
    failed_samples=$((failed_samples + 1))
    if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
      capture_failure_snapshot "navigator_exited"
    else
      capture_failure_snapshot "health_sample_failed"
    fi
  fi
}

start_seconds="$(date +%s)"
deadline_seconds=$((start_seconds + duration_seconds))
sleep "${interval_seconds}"
collect_sample

if [[ "${fault}" != "none" ]]; then
  injection_requested_count="${fault_count}"
  injection_attempted=true
  for injection_index in $(seq 1 "${injection_requested_count}"); do
    injection_attempted_count=$((injection_attempted_count + 1))
    status_before="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
    before_pid="$(printf '%s\n' "${status_before}" | awk -v target="${fault_module}" '
      $1 == "module=" target { split($3, pair, "="); print pair[2] }
    ')"
    event_recovered=false
    after_pid=0
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
        event_recovered=true
        injection_recovered_count=$((injection_recovered_count + 1))
        break
      fi
      sleep 0.1
    done
    printf '{"index":%s,"timestamp_ms":%s,"before_pid":%s,"after_pid":%s,' \
      "${injection_index}" "$(date +%s%3N)" "${before_pid}" "${after_pid}" \
      >>"${injections_path}"
    printf '"recovered":%s}\n' "${event_recovered}" >>"${injections_path}"
    if [[ "${event_recovered}" != "true" ]]; then
      injection_recovered=false
      capture_failure_snapshot "fault_recovery_failed"
      break
    fi
    collect_sample
  done
fi

while [[ "$(date +%s)" -lt "${deadline_seconds}" ]]; do
  sleep "${interval_seconds}"
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    capture_failure_snapshot "navigator_exited"
    break
  fi
  collect_sample
done

if kill -0 "${navigator_pid}" >/dev/null 2>&1; then
  runtime_alive=true
fi
if [[ ${failed_samples} -ne 0 || ${resource_unavailable_samples} -ne 0 ||
      "${injection_recovered}" != "true" ||
      "${runtime_alive}" != "true" ]]; then
  capture_failure_snapshot "stability_check_failed"
else
  report_healthy=true
fi

write_report

echo "Navigator stability report: ${output_path}"
if [[ "${report_healthy}" != "true" ]]; then
  echo "Navigator stability check failed; see ${navigator_log}" >&2
  exit 1
fi
