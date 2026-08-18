#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "Sherpa service voice smoke requires x86_64" >&2
  exit 2
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-sherpa-debug}"
wake_fixture="${COCKPIT_WAKE_FIXTURE:-${ai_root}/fixtures/nihao-xiaoshan.wav}"
command_fixture="${COCKPIT_COMMAND_FIXTURE:-${ai_root}/fixtures/open-camera-zh.wav}"
silence_fixture="${COCKPIT_SILENCE_FIXTURE:-${ai_root}/fixtures/silence.wav}"
source_config="${CONFIG_PATH:-${root_dir}/configs/development.yaml}"

for required in \
  "${runtime_root}/lib/libsherpa-onnx-c-api.so" \
  "${ai_root}/config/kws-keywords.txt" \
  "${wake_fixture}" \
  "${command_fixture}" \
  "${silence_fixture}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required Sherpa service smoke resource: ${required}" >&2
    exit 2
  fi
done

for executable in cockpit-navigator cockpit-ctl audio-probe voice-ctl; do
  if [[ ! -x "${build_dir}/bin/${executable}" ]]; then
    echo "missing ${build_dir}/bin/${executable}; build the Sherpa Debug configuration first" >&2
    exit 2
  fi
done

module_dir="${build_dir}/lib/cockpit/modules"
run_root="$(mktemp -d /tmp/cockpit-sherpa-service-voice.XXXXXX)"
config_path="${run_root}/config.yaml"
navigator_socket="${run_root}/navigator.sock"
navigator_log="${run_root}/navigator.log"

sed \
  -e "s|^  data_dir: data$|  data_dir: ${run_root}/data|" \
  -e "s|^  log_dir: logs$|  log_dir: ${run_root}/logs|" \
  -e "s|^  run_dir: run$|  run_dir: ${run_root}/run|" \
  -e "s|127.0.0.1:50050|unix:${run_root}/vehicle.sock|g" \
  -e "s|127.0.0.1:50051|unix:${run_root}/gateway.sock|g" \
  -e "s|127.0.0.1:50052|unix:${run_root}/audio.sock|g" \
  -e "s|127.0.0.1:50053|unix:${run_root}/voice.sock|g" \
  -e "s|127.0.0.1:50054|unix:${run_root}/camera.sock|g" \
  -e "s|127.0.0.1:50055|unix:${run_root}/recording.sock|g" \
  -e '/^  audio:$/,/^  camera:$/s/^    auto_start: false$/    auto_start: true/' \
  -e 's/^    capture_backend: gstreamer$/    capture_backend: synthetic/' \
  -e 's/^    input_device: .*$/    input_device: "null"/' \
  -e '0,/^    enabled: false$/s//    enabled: true/' \
  -e '/^    kws:$/,/^    vad:$/s/^      enabled: false$/      enabled: true/' \
  -e '/^    kws:$/,/^    vad:$/s/^      provider: mock$/      provider: sherpa/' \
  -e '/^    kws:$/,/^    vad:$/s/^      wake_word:.*$/      wake_word: ""\n      keywords_file: PLACEHOLDER_KEYWORDS/' \
  -e '/^    kws:$/,/^    vad:$/s|^      model_dir:.*$|      model_dir: PLACEHOLDER_KWS_MODEL|' \
  -e '/^    vad:$/,/^    speech_segment:$/s/^      provider: mock$/      provider: sherpa/' \
  -e '/^    speech_segment:$/,/^    asr:$/s/^      pre_roll_ms:.*$/      pre_roll_ms: 300/' \
  -e '/^    asr:$/,/^    tts:$/s/^      provider: mock$/      provider: sherpa-sensevoice/' \
  -e 's/^    asr_timeout_ms:.*$/    asr_timeout_ms: 10000/' \
  -e 's/^    tts_synthesis_timeout_ms:.*$/    tts_synthesis_timeout_ms: 30000/' \
  "${source_config}" | \
  sed \
    -e "s|PLACEHOLDER_KEYWORDS|${ai_root}/config/kws-keywords.txt|" \
    -e "s|PLACEHOLDER_KWS_MODEL|${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20|" \
    >"${config_path}"

navigator_pid=""
cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${build_dir}/bin/cockpit-navigator" --command shutdown --socket "${navigator_socket}" \
      >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

export COCKPIT_AI_ROOT="${ai_root}"
export LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
(
  cd "${run_root}"
  exec "${build_dir}/bin/cockpit-navigator" --config "${config_path}" \
    --module-dir "${module_dir}" --socket "${navigator_socket}" --mode ui
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

ready=false
for _ in $(seq 1 300); do
  if "${build_dir}/bin/voice-ctl" --status --config "${config_path}" >/dev/null 2>&1; then
    ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${ready}" != "true" ]]; then
  echo "Sherpa service voice stack did not become ready; log: ${navigator_log}" >&2
  tail -100 "${navigator_log}" >&2 || true
  exit 1
fi

"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null
"${build_dir}/bin/audio-probe" --start --device "wav:${wake_fixture}" \
  --config "${config_path}" >/dev/null

listening=false
for _ in $(seq 1 200); do
  wake_status="$("${build_dir}/bin/voice-ctl" --status --config "${config_path}" 2>/dev/null)"
  if [[ "${wake_status}" == *"state: listening"* ]]; then
    listening=true
    break
  fi
  sleep 0.1
done
if [[ "${listening}" != "true" ]]; then
  echo "Sherpa service voice replay did not enter listening state" >&2
  printf '%s\n' "${wake_status}" >&2
  exit 1
fi

"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null 2>&1 || true
"${build_dir}/bin/audio-probe" --start --device "wav:${command_fixture}" \
  --config "${config_path}" >/dev/null

for _ in $(seq 1 100); do
  command_audio_status="$("${build_dir}/bin/audio-probe" --status --config "${config_path}" 2>/dev/null)"
  if [[ "${command_audio_status}" == *"state: stopped"* ]]; then
    break
  fi
  sleep 0.1
done
"${build_dir}/bin/audio-probe" --start --device "wav:${silence_fixture}" \
  --config "${config_path}" >/dev/null

status=""
passed=false
for _ in $(seq 1 600); do
  status="$("${build_dir}/bin/voice-ctl" --status --config "${config_path}" 2>/dev/null)"
  if [[ "${status}" == *"actions succeeded: 1"* &&
        "${status}" == *"action_status=succeeded"* &&
        "${status}" == *"action=open_camera"* ]]; then
    passed=true
    break
  fi
  sleep 0.1
done

audio_status="$("${build_dir}/bin/audio-probe" --status --config "${config_path}" 2>/dev/null)"
printf '%s\n' "${status}"
printf '%s\n' "${audio_status}"
if [[ "${passed}" != "true" ]]; then
  echo "Sherpa service voice replay did not execute open_camera; log: ${navigator_log}" >&2
  tail -100 "${navigator_log}" >&2 || true
  exit 1
fi
if [[ "${audio_status}" != *"stream frames sent:"* ||
      "${audio_status}" == *"stream frames sent: 0"* ]]; then
  echo "Audio Driver did not send replay frames to Agent" >&2
  exit 1
fi

"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null || true
echo "Sherpa Navigator service voice replay passed; log: ${navigator_log}"
