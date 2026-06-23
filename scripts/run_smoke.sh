#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build}"
bin_dir="${build_dir}/bin"
config_path="${CONFIG_PATH:-configs/config.yaml}"
vehicle_log="${build_dir}/vehicle-data-grpc-smoke.log"
gateway_log="${build_dir}/gateway-grpc-smoke.log"
audio_log="${build_dir}/audio-grpc-smoke.log"
voice_log="${build_dir}/voice-grpc-smoke.log"

vehicle_pid=""
gateway_pid=""
audio_pid=""
voice_pid=""
cleanup() {
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
"${bin_dir}/audio-probe" --speak "System ready" --config "${config_path}"
sleep 0.1
"${bin_dir}/audio-probe" --status --config "${config_path}"
"${bin_dir}/audio-probe" --stop --config "${config_path}"
"${bin_dir}/vehicle-data-service" --config "${config_path}" --forever >"${vehicle_log}" 2>&1 &
vehicle_pid="$!"
sleep 0.2
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
