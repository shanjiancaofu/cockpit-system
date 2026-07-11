#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
vehicle_log="${build_dir}/vehicle-data-grpc-smoke.log"
gateway_log="${build_dir}/gateway-grpc-smoke.log"
audio_log="${build_dir}/audio-grpc-smoke.log"
voice_log="${build_dir}/voice-grpc-smoke.log"
camera_log="${build_dir}/camera-grpc-smoke.log"
recording_log="${build_dir}/recording-grpc-smoke.log"
recording_directory="$(realpath -m "${build_dir}/recording-smoke-$$")"

vehicle_pid=""
gateway_pid=""
audio_pid=""
voice_pid=""
camera_pid=""
recording_pid=""
cleanup() {
  if [[ -n "${recording_pid}" ]] && kill -0 "${recording_pid}" >/dev/null 2>&1; then
    kill "${recording_pid}" >/dev/null 2>&1 || true
    wait "${recording_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${camera_pid}" ]] && kill -0 "${camera_pid}" >/dev/null 2>&1; then
    kill "${camera_pid}" >/dev/null 2>&1 || true
    wait "${camera_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${voice_pid}" ]] && kill -0 "${voice_pid}" >/dev/null 2>&1; then
    kill "${voice_pid}" >/dev/null 2>&1 || true
    wait "${voice_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${audio_pid}" ]] && kill -0 "${audio_pid}" >/dev/null 2>&1; then
    kill "${audio_pid}" >/dev/null 2>&1 || true
    wait "${audio_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${gateway_pid}" ]] && kill -0 "${gateway_pid}" >/dev/null 2>&1; then
    kill "${gateway_pid}" >/dev/null 2>&1 || true
    wait "${gateway_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${vehicle_pid}" ]] && kill -0 "${vehicle_pid}" >/dev/null 2>&1; then
    kill "${vehicle_pid}" >/dev/null 2>&1 || true
    wait "${vehicle_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

"${bin_dir}/can-simulator" --config "${config_path}" --samples 3
"${bin_dir}/audio-probe" --list --config "${config_path}"
"${bin_dir}/camera-probe" --list --config "${config_path}"
"${bin_dir}/camera-service" --config "${config_path}" >"${camera_log}" 2>&1 &
camera_pid="$!"
camera_ready="false"
for _ in {1..20}; do
  if "${bin_dir}/camera-ctl" --status --config "${config_path}" >/dev/null 2>&1; then
    camera_ready="true"
    break
  fi
  sleep 0.1
done
if [[ "${camera_ready}" != "true" ]]; then
  echo "camera-service did not become ready" >&2
  exit 1
fi
"${bin_dir}/camera-ctl" --status --config "${config_path}"
camera_status_json="$(${bin_dir}/camera-ctl --status --output json --config "${config_path}")"
if [[ "${camera_status_json}" != *'"state":"CAMERA_PREVIEW_STATE_STOPPED"'* || \
      "${camera_status_json}" != *'"health"'* ]]; then
  echo "camera JSON status output is invalid" >&2
  exit 1
fi
"${bin_dir}/camera-ctl" --list --config "${config_path}"
kill "${camera_pid}"
wait "${camera_pid}" || true
camera_pid=""
cat "${camera_log}"
"${bin_dir}/audio-service" --config "${config_path}" --output-device null \
  >"${audio_log}" 2>&1 &
audio_pid="$!"
audio_ready="false"
for _ in {1..20}; do
  if "${bin_dir}/audio-probe" --status --config "${config_path}" \
      >/dev/null 2>&1; then
    audio_ready="true"
    break
  fi
  sleep 0.1
done
if [[ "${audio_ready}" != "true" ]]; then
  echo "audio-service did not become ready" >&2
  exit 1
fi
"${bin_dir}/audio-probe" --start --device null --config "${config_path}"
sleep 0.1
"${bin_dir}/audio-probe" --status --config "${config_path}"
audio_status_json="$(${bin_dir}/audio-probe --status --output json --config "${config_path}")"
if [[ "${audio_status_json}" != *'"capture_state":"CAPTURE_STATE_RUNNING"'* || \
      "${audio_status_json}" != *'"metrics"'* ]]; then
  echo "audio JSON status output is invalid" >&2
  exit 1
fi
"${bin_dir}/audio-probe" --speak "System ready" --config "${config_path}"
sleep 0.1
"${bin_dir}/audio-probe" --status --config "${config_path}"
"${bin_dir}/audio-probe" --stop --config "${config_path}"
"${bin_dir}/vehicle-data-service" --config "${config_path}" --forever >"${vehicle_log}" 2>&1 &
vehicle_pid="$!"
sleep 0.2
"${bin_dir}/recording-service" --config "${config_path}" \
  --directory "${recording_directory}" >"${recording_log}" 2>&1 &
recording_pid="$!"
recording_ready="false"
for _ in {1..20}; do
  if "${bin_dir}/recording-ctl" --config "${config_path}" >/dev/null 2>&1; then
    recording_ready="true"
    break
  fi
  sleep 0.1
done
if [[ "${recording_ready}" != "true" ]]; then
  echo "recording-service did not become ready" >&2
  exit 1
fi
printf 'smoke artifact\n' >"${recording_directory}/smoke.jpg"
"${bin_dir}/recording-ctl" --start --trigger smoke --config "${config_path}"
"${bin_dir}/recording-ctl" --event-topic /dev/smoke-event \
  --event-payload '{"ok":true,"source":"run_smoke"}' --config "${config_path}"
"${bin_dir}/recording-ctl" --file-path "${recording_directory}/smoke.jpg" --file-source camera \
  --file-kind jpeg --copy-into-session --config "${config_path}"
sleep 0.5
"${bin_dir}/recording-ctl" --config "${config_path}"
"${bin_dir}/recording-ctl" --stop --config "${config_path}"
recording_list="$(${bin_dir}/recording-ctl --list --config "${config_path}")"
echo "${recording_list}"
if [[ "${recording_list}" != *"total sessions: 1"* || \
      "${recording_list}" != *"state=complete"* ]]; then
  echo "recording smoke did not list the completed session" >&2
  exit 1
fi
"${bin_dir}/recording-ctl" --prune --config "${config_path}"
if ! find "${recording_directory}/sessions" -name COMPLETE -type f -print -quit | grep -q .; then
  echo "recording smoke did not create a COMPLETE session" >&2
  exit 1
fi
if ! find "${recording_directory}/sessions" -name vehicle_state.jsonl -type f \
    -size +0c -print -quit | grep -q .; then
  echo "recording smoke did not persist vehicle states" >&2
  exit 1
fi
if ! find "${recording_directory}/sessions" -name events.jsonl -type f \
    -size +0c -print -quit | grep -q .; then
  echo "recording smoke did not persist events" >&2
  exit 1
fi
recording_session_id="$(printf '%s\n' "${recording_list}" | awk '/state=complete/{print $1; exit}')"
if [[ -z "${recording_session_id}" ]]; then
  echo "recording smoke could not resolve the completed session id" >&2
  exit 1
fi
recording_detail="$(${bin_dir}/recording-ctl --detail "${recording_session_id}" \
  --config "${config_path}")"
echo "${recording_detail}"
if [[ "${recording_detail}" != *"data files indexed: 1"* || \
      "${recording_detail}" != *"config checksum: fnv1a64:"* || \
      "${recording_detail}" != *"git commit: "* || \
      "${recording_detail}" != *"binary version: 0.1.0"* ]]; then
  echo "recording smoke did not expose replay metadata" >&2
  exit 1
fi
recording_timeline="$(${bin_dir}/recording-ctl --timeline "${recording_session_id}" \
  --limit 20 --config "${config_path}")"
echo "${recording_timeline}"
if [[ "${recording_timeline}" != *"kind=event"* || \
      "${recording_timeline}" != *"kind=data_file"* || \
      "${recording_timeline}" != *"kind=vehicle_state"* ]]; then
  echo "recording smoke did not expose the merged timeline" >&2
  exit 1
fi
recording_verification="$(${bin_dir}/recording-ctl --verify "${recording_session_id}" \
  --config "${config_path}")"
echo "${recording_verification}"
if [[ "${recording_verification}" != *"healthy: true"* || \
      "${recording_verification}" != *"files checked: 1"* || \
      "${recording_verification}" != *"checksums checked: 1"* ]]; then
  echo "recording smoke integrity verification failed" >&2
  exit 1
fi
recording_batch_verification="$(${bin_dir}/recording-ctl --verify-all --limit 10 --output json \
  --config "${config_path}")"
echo "${recording_batch_verification}"
if [[ "${recording_batch_verification}" != *'"total_sessions":"1"'* || \
      "${recording_batch_verification}" != *'"healthy_sessions":"1"'* || \
      "${recording_batch_verification}" != *'"damaged_sessions":"0"'* || \
      "${recording_batch_verification}" != *'"unavailable_sessions":"0"'* ]]; then
  echo "recording smoke batch integrity verification failed" >&2
  exit 1
fi
"${bin_dir}/recording-ctl" --delete "${recording_session_id}" --config "${config_path}"
recording_list_after_delete="$(${bin_dir}/recording-ctl --list --config "${config_path}")"
echo "${recording_list_after_delete}"
if [[ "${recording_list_after_delete}" != *"total sessions: 0"* ]]; then
  echo "recording smoke did not delete the completed session" >&2
  exit 1
fi
kill "${recording_pid}"
wait "${recording_pid}" || true
recording_pid=""
cat "${recording_log}"
"${bin_dir}/cockpit-gateway-service" --config "${config_path}" \
  >"${gateway_log}" 2>&1 &
gateway_pid="$!"
sleep 0.2
"${bin_dir}/topic" list --backend grpc --config "${config_path}"
"${bin_dir}/topic" info /vehicle/state --backend grpc --config "${config_path}"
"${bin_dir}/topic" echo /vehicle/state --backend grpc --count 1 --config "${config_path}"
"${bin_dir}/voice-interaction-service" --enable --config "${config_path}" \
  >"${voice_log}" 2>&1 &
voice_pid="$!"
voice_ready="false"
for _ in {1..20}; do
  if "${bin_dir}/voice-ctl" --status --config "${config_path}" \
      >/dev/null 2>&1; then
    voice_ready="true"
    break
  fi
  sleep 0.1
done
if [[ "${voice_ready}" != "true" ]]; then
  echo "voice-interaction-service did not become ready" >&2
  exit 1
fi
fresh_vehicle_state="false"
for _ in {1..20}; do
  if "${bin_dir}/topic" echo /vehicle/state --backend grpc --count 1 \
      --timeout-ms 1000 --config "${config_path}" >/dev/null 2>&1; then
    fresh_vehicle_state="true"
    break
  fi
  sleep 0.2
done
if [[ "${fresh_vehicle_state}" != "true" ]]; then
  echo "gateway did not provide a fresh vehicle state for voice smoke" >&2
  exit 1
fi
vehicle_voice_response="$("${bin_dir}/voice-ctl" --process "show vehicle status" --config "${config_path}")"
echo "${vehicle_voice_response}"
if [[ "${vehicle_voice_response}" != *"action_status=succeeded"* || \
      "${vehicle_voice_response}" != *"Vehicle speed is"* ]]; then
  echo "voice vehicle status did not return expected live response" >&2
  exit 1
fi
music_response="$("${bin_dir}/voice-ctl" --process "play music" --config "${config_path}")"
echo "${music_response}"
if [[ "${music_response}" != *"HMI command recorded locally"* || \
      "${music_response}" != *"play_music"* ]]; then
  echo "voice play music handoff did not return expected placeholder" >&2
  exit 1
fi
camera_response="$("${bin_dir}/voice-ctl" --process "open camera" --config "${config_path}")"
echo "${camera_response}"
if [[ "${camera_response}" != *"HMI command recorded locally"* || \
      "${camera_response}" != *"open_camera_preview"* ]]; then
  echo "voice open camera handoff did not return expected placeholder" >&2
  exit 1
fi
sleep 0.1
"${bin_dir}/voice-ctl" --status --config "${config_path}"
voice_status_json="$(${bin_dir}/voice-ctl --status --output json --config "${config_path}")"
if [[ "${voice_status_json}" != *'"state":"INTERACTION_STATE_LISTENING"'* || \
      "${voice_status_json}" != *'"metrics"'* ]]; then
  echo "voice JSON status output is invalid" >&2
  exit 1
fi
"${bin_dir}/audio-probe" --status --config "${config_path}"
kill "${voice_pid}"
wait "${voice_pid}" || true
voice_pid=""
cat "${voice_log}"
"${bin_dir}/topic" echo /vehicle/state --backend grpc --count 3 --config "${config_path}"
"${bin_dir}/topic" hz /vehicle/state --backend grpc --window 3 --count 3 \
  --config "${config_path}"
kill "${gateway_pid}"
wait "${gateway_pid}" || true
gateway_pid=""
kill "${vehicle_pid}"
wait "${vehicle_pid}" || true
vehicle_pid=""
kill "${audio_pid}"
wait "${audio_pid}" || true
audio_pid=""
cat "${audio_log}"
cat "${vehicle_log}"
cat "${gateway_log}"
"${bin_dir}/cloud-uplink-service" --config "${config_path}" --once
"${bin_dir}/topic" pub /dev/smoke '{"ok":true,"source":"run_smoke"}' --repeat 3 --rate-ms 20 --config "${config_path}"
"${bin_dir}/topic" list --config "${config_path}"
"${bin_dir}/topic" echo /dev/smoke --tail 1 --config "${config_path}"
"${bin_dir}/topic" hz /dev/smoke --window 3 --config "${config_path}"
