#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

build_dir="$(realpath -m "${BUILD_DIR:-$(cockpit_default_debug_build_dir)}")"
runtime_dir="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
bin_dir="${build_dir}/bin"
module_dir="${build_dir}/lib/cockpit/modules"
source_config="$(realpath "${CONFIG_PATH:-${root_dir}/configs/development.yaml}")"
run_dir="${runtime_dir}/run/smoke-${BASHPID}"
export COCKPIT_RUNTIME_DIR="${run_dir}"
config_path="${run_dir}/config.yaml"
socket_path="${run_dir}/navigator.sock"
navigator_log="${run_dir}/navigator.log"
recording_directory="${COCKPIT_RUNTIME_DIR}/data/recordings"
expected_modules=(transfer vehicle_driver audio_driver camera_driver agent recording)
if [[ -x "${bin_dir}/cockpit-ui" ]]; then
  expected_modules+=(hmi)
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
fi

for executable in cockpit-navigator cockpit-ctl audio-probe camera-ctl camera-probe \
  can-simulator recording-ctl topic voice-ctl; do
  if [[ ! -x "${bin_dir}/${executable}" ]]; then
    echo "missing ${bin_dir}/${executable}; run bash scripts/build.sh first" >&2
    exit 2
  fi
done
if [[ ! -d "${module_dir}" ]]; then
  echo "missing Navigator modules under ${module_dir}" >&2
  exit 2
fi

mkdir -p "${run_dir}"
awk '
  /^features:$/ { in_features = 1 }
  in_features && /^  voice:$/ { in_voice = 1 }
  in_voice && /^    enabled: false$/ {
    sub(/false$/, "true")
    in_voice = 0
  }
  /^    output_device: default$/ { sub(/default$/, "\"null\"") }
  /^    capture_backend: gstreamer$/ { sub(/gstreamer$/, "synthetic") }
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
    --socket "${socket_path}" --mode development
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
  echo "Navigator development mode did not become ready; see ${navigator_log}" >&2
  exit 1
fi

runtime_status="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
for module in "${expected_modules[@]}"; do
  if [[ "${runtime_status}" != *"module=${module} state=running"* ]]; then
    echo "Navigator module ${module} is not running" >&2
    exit 1
  fi
done
health_json="$("${bin_dir}/cockpit-ctl" health --mode development --output json \
  --config "${config_path}")"
if [[ "${health_json}" != *'"healthy":true'* ]]; then
  echo "Navigator development services are not healthy" >&2
  exit 1
fi

"${bin_dir}/can-simulator" --config "${config_path}" --samples 3
"${bin_dir}/audio-probe" --list --config "${config_path}"
"${bin_dir}/camera-probe" --list --config "${config_path}"

camera_status_json="$("${bin_dir}/camera-ctl" --status --output json --config "${config_path}")"
if [[ "${camera_status_json}" != *'"state":"CAMERA_PREVIEW_STATE_STOPPED"'* ||
      "${camera_status_json}" != *'"health"'* ]]; then
  echo "camera JSON status output is invalid" >&2
  exit 1
fi
"${bin_dir}/camera-ctl" --list --config "${config_path}"

"${bin_dir}/audio-probe" --start --device null --config "${config_path}"
sleep 0.1
audio_status_json="$("${bin_dir}/audio-probe" --status --output json --config "${config_path}")"
if [[ "${audio_status_json}" != *'"capture_state":"CAPTURE_STATE_RUNNING"'* ||
      "${audio_status_json}" != *'"metrics"'* ]]; then
  echo "audio JSON status output is invalid" >&2
  exit 1
fi
"${bin_dir}/audio-probe" --speak "System ready" --config "${config_path}"
sleep 0.1
"${bin_dir}/audio-probe" --stop --config "${config_path}"

mkdir -p "${recording_directory}"
printf 'smoke artifact\n' >"${recording_directory}/smoke.jpg"
"${bin_dir}/recording-ctl" --start --trigger smoke --config "${config_path}"
"${bin_dir}/recording-ctl" --event-topic /dev/smoke-event \
  --event-payload '{"ok":true,"source":"run_smoke"}' --config "${config_path}"
"${bin_dir}/recording-ctl" --file-path "${recording_directory}/smoke.jpg" \
  --file-source camera --file-kind jpeg --copy-into-session --config "${config_path}"
sleep 0.5
"${bin_dir}/recording-ctl" --stop --config "${config_path}"
recording_list="$("${bin_dir}/recording-ctl" --list --config "${config_path}")"
echo "${recording_list}"
if [[ "${recording_list}" != *"total sessions: 1"* ||
      "${recording_list}" != *"state=complete"* ]]; then
  echo "recording smoke did not list the completed session" >&2
  exit 1
fi
"${bin_dir}/recording-ctl" --prune --config "${config_path}"
if [[ -z "$(find "${recording_directory}/sessions" -name COMPLETE -type f -print -quit)" ||
      -z "$(find "${recording_directory}/sessions" -name vehicle_state.jsonl -type f \
        -size +0c -print -quit)" ||
      -z "$(find "${recording_directory}/sessions" -name events.jsonl -type f \
        -size +0c -print -quit)" ]]; then
  echo "recording smoke output is incomplete" >&2
  exit 1
fi

recording_session_id="$(printf '%s\n' "${recording_list}" | awk '/state=complete/{print $1; exit}')"
recording_detail="$("${bin_dir}/recording-ctl" --detail "${recording_session_id}" \
  --config "${config_path}")"
echo "${recording_detail}"
if [[ "${recording_detail}" != *"data files indexed: 1"* ||
      "${recording_detail}" != *"config checksum: fnv1a64:"* ||
      "${recording_detail}" != *"git commit: "* ||
      "${recording_detail}" != *"binary version: 0.1.0"* ]]; then
  echo "recording smoke did not expose replay metadata" >&2
  exit 1
fi
recording_timeline="$("${bin_dir}/recording-ctl" --timeline "${recording_session_id}" \
  --limit 20 --config "${config_path}")"
if [[ "${recording_timeline}" != *"kind=event"* ||
      "${recording_timeline}" != *"kind=data_file"* ||
      "${recording_timeline}" != *"kind=vehicle_state"* ]]; then
  echo "recording smoke did not expose the merged timeline" >&2
  exit 1
fi
recording_verification="$("${bin_dir}/recording-ctl" --verify "${recording_session_id}" \
  --config "${config_path}")"
if [[ "${recording_verification}" != *"healthy: true"* ||
      "${recording_verification}" != *"files checked: 1"* ||
      "${recording_verification}" != *"checksums checked: 1"* ]]; then
  echo "recording smoke integrity verification failed" >&2
  exit 1
fi
recording_report="$("${bin_dir}/recording-ctl" --report "${recording_session_id}" \
  --timeline-limit 20 --issue-limit 20 --output json --config "${config_path}")"
if [[ "${recording_report}" != *'"detail"'* ||
      "${recording_report}" != *'"timeline"'* ||
      "${recording_report}" != *'"integrity"'* ||
      "${recording_report}" != *'"healthy":true'* ||
      "${recording_report}" != *'"data_files_indexed":"1"'* ]]; then
  echo "recording smoke report is incomplete" >&2
  exit 1
fi
recording_batch="$("${bin_dir}/recording-ctl" --verify-all --limit 10 --output json \
  --config "${config_path}")"
if [[ "${recording_batch}" != *'"total_sessions":"1"'* ||
      "${recording_batch}" != *'"healthy_sessions":"1"'* ||
      "${recording_batch}" != *'"damaged_sessions":"0"'* ]]; then
  echo "recording smoke batch integrity verification failed" >&2
  exit 1
fi
"${bin_dir}/recording-ctl" --delete "${recording_session_id}" --config "${config_path}"

"${bin_dir}/topic" list --backend grpc --config "${config_path}"
topic_info="$("${bin_dir}/topic" info /vehicle/state --backend grpc --config "${config_path}")"
echo "${topic_info}"
if [[ "${topic_info}" != *"transport: grpc"* ||
      "${topic_info}" != *"expected update period ms: 100"* ||
      "${topic_info}" != *"availability: available"* ||
      "${topic_info}" != *"error reason: none"* ]]; then
  echo "topic metadata is incomplete" >&2
  exit 1
fi
"${bin_dir}/topic" echo /vehicle/state --backend grpc --count 1 --config "${config_path}"
vehicle_response="$("${bin_dir}/voice-ctl" --process "show vehicle status" \
  --config "${config_path}")"
if [[ "${vehicle_response}" != *"action_status=succeeded"* ||
      "${vehicle_response}" != *"Vehicle speed is"* ]]; then
  echo "voice vehicle status did not return expected live response" >&2
  exit 1
fi
camera_response="$("${bin_dir}/voice-ctl" --process "open camera" --config "${config_path}")"
music_response="$("${bin_dir}/voice-ctl" --process "play music" --config "${config_path}")"
if [[ "${camera_response}" != *"action_status=succeeded"* ||
      "${camera_response}" != *"Camera view opened."* ||
      "${music_response}" != *"action_status=failed"* ||
      "${music_response}" != *"Media player is not connected."* ]]; then
  echo "voice HMI execution did not return expected responses" >&2
  exit 1
fi
interrupt_json="$("${bin_dir}/voice-ctl" --interrupt --output json --config "${config_path}")"
if [[ "${interrupt_json}" != *'"active_request_interrupted":false'* ||
      "${interrupt_json}" != *'"queued_transcripts_discarded":"0"'* ]]; then
  echo "voice interrupt control output is invalid" >&2
  exit 1
fi
voice_status_json="$("${bin_dir}/voice-ctl" --status --output json --config "${config_path}")"
if [[ "${voice_status_json}" != *'"state":"INTERACTION_STATE_LISTENING"'* ||
      "${voice_status_json}" != *'"requests_interrupted":"0"'* ||
      "${voice_status_json}" != *'"provider_timeouts":"0"'* ||
      "${voice_status_json}" != *'"provider_failures":"0"'* ]]; then
  echo "voice JSON status output is invalid" >&2
  exit 1
fi
snapshot_dir="${run_dir}/diagnostic-snapshot"
"${bin_dir}/cockpit-ctl" snapshot --config "${config_path}" --socket "${socket_path}" \
  --directory "${snapshot_dir}" --max-log-bytes 4096
if ! grep -q '"runtime":{"available":true' "${snapshot_dir}/manifest.json" ||
   ! grep -q '"services"' "${snapshot_dir}/service_status.json" ||
   ! grep -q 'module=agent state=running' "${snapshot_dir}/runtime_status.txt"; then
  echo "online diagnostic snapshot is incomplete" >&2
  exit 1
fi
"${bin_dir}/topic" hz /vehicle/state --backend grpc --window 3 --count 3 \
  --config "${config_path}"

"${bin_dir}/cockpit-ctl" runtime switch cloud --socket "${socket_path}" >/dev/null
cloud_status="$("${bin_dir}/cockpit-ctl" runtime status --socket "${socket_path}")"
if [[ "${cloud_status}" != *"mode=cloud"* ||
      "${cloud_status}" != *"module=carupload state=running"* ]]; then
  echo "Navigator cloud mode did not start carupload" >&2
  exit 1
fi
"${bin_dir}/cockpit-ctl" runtime switch development --socket "${socket_path}" >/dev/null

"${bin_dir}/topic" pub /dev/smoke '{"ok":true,"source":"run_smoke"}' \
  --repeat 3 --rate-ms 20 --config "${config_path}"
"${bin_dir}/topic" echo /dev/smoke --tail 1 --config "${config_path}"
"${bin_dir}/topic" hz /dev/smoke --window 3 --config "${config_path}"

echo "Navigator smoke passed; log: ${navigator_log}"
