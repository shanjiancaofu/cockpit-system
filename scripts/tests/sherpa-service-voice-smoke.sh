#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"


cockpit_native_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "x86_64" ;;
    aarch64|arm64) echo "arm64" ;;
    *) echo "unsupported native architecture: $(uname -m)" >&2; return 1 ;;
  esac
}

cockpit_output_dir() { echo "${COCKPIT_OUTPUT_DIR:-${root_dir}/_output}"; }
cockpit_default_debug_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-debug"; }
cockpit_default_release_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-release"; }
cockpit_default_runtime_dir() { echo "$(cockpit_output_dir)/runtime"; }


if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "Sherpa service voice smoke requires x86_64" >&2
  exit 2
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-sherpa-debug}"
wake_fixture="${COCKPIT_WAKE_FIXTURE:-${ai_root}/fixtures/nihao-xiaoshan.wav}"
command_fixture="${COCKPIT_COMMAND_FIXTURE:-${ai_root}/fixtures/open-camera-zh.wav}"
expected_action="${COCKPIT_EXPECTED_ACTION:-open_camera}"
media_focus_smoke="${COCKPIT_MEDIA_FOCUS_SMOKE:-false}"
media_manifest="${COCKPIT_MEDIA_MANIFEST:-${root_dir}/_output/media/manifest.yaml}"
media_focus_probe_fixture="${COCKPIT_MEDIA_FOCUS_PROBE_FIXTURE:-${ai_root}/fixtures/live/vehicle-status.wav}"
media_focus_output_device="${COCKPIT_MEDIA_FOCUS_OUTPUT_DEVICE:-}"
silence_fixture="${COCKPIT_SILENCE_FIXTURE:-${ai_root}/fixtures/silence.wav}"
negative_fixture="${COCKPIT_NEGATIVE_FIXTURE:-${ai_root}/fixtures/live/segment-06-negative-commands.wav}"
retired_wake_fixture="${COCKPIT_RETIRED_WAKE_FIXTURE:-${ai_root}/fixtures/nihao-xiaoche.wav}"
source_config="${CONFIG_PATH:-${root_dir}/configs/development.yaml}"
service_repetitions="${COCKPIT_SERVICE_VOICE_REPETITIONS:-1}"

if ! [[ "${service_repetitions}" =~ ^[1-9][0-9]*$ ]] || ((service_repetitions > 32)); then
  echo "COCKPIT_SERVICE_VOICE_REPETITIONS must be an integer from 1 through 32" >&2
  exit 2
fi
warmup_repetition=3
if ((service_repetitions < warmup_repetition)); then
  warmup_repetition=${service_repetitions}
fi

for required in \
  "${runtime_root}/lib/libsherpa-onnx-c-api.so" \
  "${ai_root}/config/kws-keywords.txt" \
  "${wake_fixture}" \
  "${command_fixture}" \
  "${silence_fixture}" \
  "${negative_fixture}" \
  "${retired_wake_fixture}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required Sherpa service smoke resource: ${required}" >&2
    exit 2
  fi
done

if [[ "${media_focus_smoke}" == "true" ]]; then
  for required in "${media_manifest}" "${media_focus_probe_fixture}"; do
    if [[ ! -f "${required}" ]]; then
      echo "missing media focus smoke resource: ${required}" >&2
      exit 2
    fi
  done
fi

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
  -e "s|^  data_dir: _output/runtime/data$|  data_dir: ${run_root}/data|" \
  -e "s|^  log_dir: _output/runtime/logs$|  log_dir: ${run_root}/logs|" \
  -e "s|^  run_dir: _output/runtime/run$|  run_dir: ${run_root}/run|" \
  -e "s|127.0.0.1:50050|unix:${run_root}/vehicle.sock|g" \
  -e "s|127.0.0.1:50051|unix:${run_root}/gateway.sock|g" \
  -e "s|127.0.0.1:50052|unix:${run_root}/audio.sock|g" \
  -e "s|127.0.0.1:50053|unix:${run_root}/voice.sock|g" \
  -e "s|127.0.0.1:50054|unix:${run_root}/camera.sock|g" \
  -e "s|127.0.0.1:50055|unix:${run_root}/recording.sock|g" \
  -e "s|127.0.0.1:50056|unix:${run_root}/media.sock|g" \
  -e "s|127.0.0.1:50057|unix:${run_root}/sentinel.sock|g" \
  -e "s|127.0.0.1:50058|unix:${run_root}/bridge.sock|g" \
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
  -e '/^    tts:$/,/^  ai:$/s/^      provider: mock$/      provider: sherpa-kokoro/' \
  -e 's/^    asr_timeout_ms:.*$/    asr_timeout_ms: 10000/' \
  -e 's/^    tts_synthesis_timeout_ms:.*$/    tts_synthesis_timeout_ms: 30000/' \
  "${source_config}" | \
  sed \
    -e "s|PLACEHOLDER_KEYWORDS|${ai_root}/config/kws-keywords.txt|" \
    -e "s|PLACEHOLDER_KWS_MODEL|${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20|" \
    >"${config_path}"

if [[ "${media_focus_smoke}" == "true" ]]; then
  sed -i \
    -e '/^  media:$/,/^  recording:$/s/^    provider: disabled$/    provider: gstreamer/' \
    -e "/^  media:$/,/^  recording:$/s|^    manifest:.*$|    manifest: ${media_manifest}|" \
    -e '/^  media:$/,/^  recording:$/s/^    sink: fakesink$/    sink: alsasink/' \
    "${config_path}"
  if [[ -n "${media_focus_output_device}" ]]; then
    sed -i "/^  audio:$/,/^  camera:$/s|^    output_device:.*$|    output_device: ${media_focus_output_device}|" \
      "${config_path}"
  fi
fi

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

peak_service_rss_kib=0
current_service_rss_kib=0
warmup_service_rss_kib=0
update_service_rss() {
  local child_pids
  local process_pids
  local rss_kib
  child_pids="$(<"/proc/${navigator_pid}/task/${navigator_pid}/children")"
  process_pids="${navigator_pid}${child_pids:+,${child_pids// /,}}"
  rss_kib="$(ps -o rss= -p "${process_pids%,}" 2>/dev/null | awk '{total += $1} END {print total + 0}')"
  if [[ "${rss_kib}" =~ ^[0-9]+$ ]]; then
    current_service_rss_kib=${rss_kib}
    if ((rss_kib > peak_service_rss_kib)); then
      peak_service_rss_kib=${rss_kib}
    fi
  fi
}

run_positive_replay() {
  local expected_actions="$1"
  local listening=false
  local passed=false
  local playback_completed=false
  local status=""
  local audio_status=""

  if ((expected_actions == 1)); then
    "${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null 2>&1 || true
    "${build_dir}/bin/audio-probe" --start --device "wav:${wake_fixture}" \
      --config "${config_path}" >/dev/null
    for _ in $(seq 1 200); do
      update_service_rss
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
      return 1
    fi
  else
    for _ in $(seq 1 450); do
      status="$("${build_dir}/bin/voice-ctl" --status --config "${config_path}" 2>/dev/null)"
      if [[ "${status}" == *"state: follow_up"* ]]; then
        break
      fi
      sleep 0.1
    done
    if [[ "${status}" != *"state: follow_up"* ]]; then
      echo "previous positive replay did not enter the follow-up window" >&2
      printf '%s\n' "${status}" >&2
      return 1
    fi
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

  for _ in $(seq 1 600); do
    update_service_rss
    status="$("${build_dir}/bin/voice-ctl" --status --config "${config_path}" 2>/dev/null)"
    if [[ "${status}" == *"actions succeeded: ${expected_actions}"* &&
          "${status}" == *"action_status=succeeded"* &&
          "${status}" == *"action=${expected_action}"* ]]; then
      passed=true
      break
    fi
    sleep 0.1
  done

  for _ in $(seq 1 450); do
    update_service_rss
    audio_status="$("${build_dir}/bin/audio-probe" --status --config "${config_path}" 2>/dev/null)"
    if [[ "${audio_status}" == *"playback played: ${expected_actions}"* &&
          "${audio_status}" == *"playback failed: 0"* &&
          "${audio_status}" == *"playback dropped: 0"* ]]; then
      playback_completed=true
      break
    fi
    sleep 0.1
  done
  if ((expected_actions == 1 || expected_actions == service_repetitions)); then
    printf 'positive replay %s/%s\n%s\n%s\n' "${expected_actions}" "${service_repetitions}" \
      "${status}" "${audio_status}"
  else
    printf 'positive replay %s/%s passed\n' "${expected_actions}" "${service_repetitions}"
  fi
  if [[ "${passed}" != "true" ]]; then
    echo "Sherpa service voice replay did not execute ${expected_action}; log: ${navigator_log}" >&2
    tail -100 "${navigator_log}" >&2 || true
    return 1
  fi
  if [[ "${playback_completed}" != "true" ]]; then
    echo "Sherpa service voice replay did not receive successful playback completion" >&2
    return 1
  fi
  if [[ "${audio_status}" != *"stream frames sent:"* ||
        "${audio_status}" == *"stream frames sent: 0"* ]]; then
    echo "Audio Driver did not send replay frames to Agent" >&2
    return 1
  fi
}

for repetition in $(seq 1 "${service_repetitions}"); do
  run_positive_replay "${repetition}"
  update_service_rss
  if ((repetition == warmup_repetition)); then
    warmup_service_rss_kib=${current_service_rss_kib}
  fi
done

if [[ "${media_focus_smoke}" == "true" ]]; then
  focus_ready=false
  for _ in $(seq 1 450); do
    status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
    if [[ "${status}" == *"state: follow_up"* ]]; then
      focus_ready=true
      break
    fi
    sleep 0.1
  done
  if [[ "${focus_ready}" != "true" ]]; then
    echo "play_music response did not reach follow-up before focus probe" >&2
    exit 1
  fi

  "${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null 2>&1 || true
  "${build_dir}/bin/audio-probe" --start --device "wav:${media_focus_probe_fixture}" \
    --config "${config_path}" >/dev/null
  for _ in $(seq 1 200); do
    audio_status="$(${build_dir}/bin/audio-probe --status --config "${config_path}" 2>/dev/null)"
    if [[ "${audio_status}" == *"state: stopped"* ]]; then
      break
    fi
    sleep 0.1
  done
  "${build_dir}/bin/audio-probe" --start --device "wav:${silence_fixture}" \
    --config "${config_path}" >/dev/null
  focus_probe_completed=false
  playing_line=""
  paused_line=""
  submit_line=""
  receipt_line=""
  resumed_line=""
  for _ in $(seq 1 600); do
    status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
    playing_line="$(grep -n 'audio focus observed media state=playing' "${navigator_log}" | tail -1 | cut -d: -f1 || true)"
    paused_line="$(grep -n 'audio focus acquire confirmed media state=paused' "${navigator_log}" | tail -1 | cut -d: -f1 || true)"
    submit_line="$(grep -n 'voice playback submitted after audio focus acquire' "${navigator_log}" | tail -1 | cut -d: -f1 || true)"
    receipt_line="$(grep -n 'voice playback receipt completed before audio focus release' "${navigator_log}" | tail -1 | cut -d: -f1 || true)"
    resumed_line="$(grep -n 'audio focus release confirmed media state=playing' "${navigator_log}" | tail -1 | cut -d: -f1 || true)"
    if [[ "${status}" == *"actions succeeded: 2"* && -n "${playing_line}" &&
          -n "${paused_line}" && -n "${submit_line}" && -n "${receipt_line}" &&
          -n "${resumed_line}" ]] &&
       ((playing_line < paused_line && paused_line < submit_line && submit_line < receipt_line &&
         receipt_line < resumed_line)); then
      focus_probe_completed=true
      break
    fi
    sleep 0.1
  done

  if [[ "${focus_probe_completed}" != "true" ]]; then
    echo "Voice/Media focus did not confirm PLAYING -> PAUSED -> TTS receipt -> PLAYING" >&2
    tail -120 "${navigator_log}" >&2 || true
    exit 1
  fi
  echo "Voice/Media focus PLAYING -> PAUSED -> TTS receipt -> PLAYING verified"
  audio_status="$(${build_dir}/bin/audio-probe --status --config "${config_path}" 2>/dev/null)"
  if [[ "${audio_status}" != *"playback failed: 0"* ||
        "${audio_status}" != *"playback dropped: 0"* ||
        "${audio_status}" != *"xruns: 0"* ||
        "${audio_status}" != *"device errors: 0"* ]]; then
    echo "Audio Driver reported errors during Voice/Media focus smoke" >&2
    printf '%s\n' "${audio_status}" >&2
    exit 1
  fi
  "${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null || true
  echo "Sherpa Navigator Voice/Media focus smoke passed; log: ${navigator_log}"
  exit 0
fi
update_service_rss
final_service_rss_kib=${current_service_rss_kib}
post_warmup_growth_kib=0
if ((final_service_rss_kib > warmup_service_rss_kib)); then
  post_warmup_growth_kib=$((final_service_rss_kib - warmup_service_rss_kib))
fi
echo "service RSS: warmup_repetition=${warmup_repetition} warmup_service_rss_kib=${warmup_service_rss_kib} final_service_rss_kib=${final_service_rss_kib} peak_service_rss_kib=${peak_service_rss_kib} post_warmup_growth_kib=${post_warmup_growth_kib}"
if [[ "${media_focus_smoke}" != "true" ]] && ((post_warmup_growth_kib > 64 * 1024)); then
  echo "service process tree RSS grew more than 64 MiB after warm-up" >&2
  exit 1
fi
audio_status="$("${build_dir}/bin/audio-probe" --status --config "${config_path}" 2>/dev/null)"
if [[ "${audio_status}" != *"playback failed: 0"* ||
      "${audio_status}" != *"playback dropped: 0"* ||
      "${audio_status}" != *"xruns: 0"* ||
      "${audio_status}" != *"device errors: 0"* ]]; then
  echo "Audio Driver reported playback/capture errors during repeated replay" >&2
  printf '%s\n' "${audio_status}" >&2
  exit 1
fi
echo "repeated positive replay passed: repetitions=${service_repetitions} warmup_service_rss_kib=${warmup_service_rss_kib} final_service_rss_kib=${final_service_rss_kib} peak_service_rss_kib=${peak_service_rss_kib} post_warmup_growth_kib=${post_warmup_growth_kib}"

# Reuse the same live service to prove a negative follow-up cannot complete a second
# action. SenseVoice may return unknown or a typed action whose consumer is unavailable;
# neither result may increase the successful action count.
for _ in $(seq 1 450); do
  status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
  if [[ "${status}" == *"state: follow_up"* ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${status}" != *"state: follow_up"* ]]; then
  echo "positive command did not enter the follow-up window" >&2
  printf '%s\n' "${status}" >&2
  exit 1
fi
"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null 2>&1 || true
"${build_dir}/bin/audio-probe" --start --device "wav:${negative_fixture}" \
  --config "${config_path}" >/dev/null
for _ in $(seq 1 100); do
  command_audio_status="$(${build_dir}/bin/audio-probe --status --config "${config_path}" 2>/dev/null)"
  if [[ "${command_audio_status}" == *"state: stopped"* ]]; then
    break
  fi
  sleep 0.1
done
"${build_dir}/bin/audio-probe" --start --device "wav:${silence_fixture}" \
  --config "${config_path}" >/dev/null
negative_passed=false
for _ in $(seq 1 200); do
  status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
  if [[ "${status}" == *"actions succeeded: ${service_repetitions}"* ]] &&
     { ((service_repetitions > 1)) || [[ "${status}" == *"transcripts received: 3"* ]]; }; then
    negative_passed=true
    break
  fi
  sleep 0.1
done
if [[ "${negative_passed}" != "true" ]]; then
  echo "negative follow-up completed an unexpected action" >&2
  printf '%s\n' "${status}" >&2
  exit 1
fi

for _ in $(seq 1 200); do
  status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
  if [[ "${status}" == *"state: idle"* ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${status}" != *"state: idle"* ]]; then
  echo "negative response did not leave the follow-up window" >&2
  exit 1
fi
"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null 2>&1 || true
"${build_dir}/bin/audio-probe" --start --device "wav:${retired_wake_fixture}" \
  --config "${config_path}" >/dev/null
for _ in $(seq 1 100); do
  retired_audio_status="$(${build_dir}/bin/audio-probe --status --config "${config_path}" 2>/dev/null)"
  if [[ "${retired_audio_status}" == *"state: stopped"* ]]; then
    break
  fi
  sleep 0.1
done
sleep 1
status="$(${build_dir}/bin/voice-ctl --status --config "${config_path}" 2>/dev/null)"
if [[ "${status}" != *"actions succeeded: ${service_repetitions}"* ||
      ("${service_repetitions}" == "1" && "${status}" != *"transcripts received: 3"*) ]]; then
  echo "retired wake word caused an unexpected interaction" >&2
  printf '%s\n' "${status}" >&2
  exit 1
fi
echo "service safety replay passed: no unexpected action succeeded and retired wake was ignored"

"${build_dir}/bin/audio-probe" --stop --config "${config_path}" >/dev/null || true
echo "Sherpa Navigator service voice replay passed; log: ${navigator_log}"
