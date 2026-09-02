#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Cockpit full-system soak requires the ARM64 Jetson target" >&2
  exit 2
fi

duration_seconds="${COCKPIT_SOAK_DURATION_SECONDS:-1200}"
interaction_interval_seconds="${COCKPIT_SOAK_INTERACTION_INTERVAL_SECONDS:-180}"
llm_fault_at_override="${COCKPIT_SOAK_LLM_FAULT_AT_SECONDS:-}"
camera_fault_at_override="${COCKPIT_SOAK_CAMERA_FAULT_AT_SECONDS:-}"
build_dir="$(realpath -m "${BUILD_DIR:-${root_dir}/_output/build/arm64-full-system-release}")"
ai_root="$(realpath -m "${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}")"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
source_config="${CONFIG_PATH:-${root_dir}/configs/development.yaml}"
display="${DISPLAY:-:0}"
xauthority="${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}"
run_root="$(mktemp -d "${root_dir}/_output/runtime/run/full-system-soak.XXXXXX")"
config_path="${run_root}/config.yaml"
navigator_socket="${run_root}/navigator.sock"
navigator_log="${run_root}/navigator.log"
raw_report="${run_root}/full-system-runtime.json"
summary_report="${run_root}/full-system-runtime.txt"

if ! [[ "${duration_seconds}" =~ ^[1-9][0-9]*$ ]] || ((duration_seconds < 300)); then
  echo "COCKPIT_SOAK_DURATION_SECONDS must be an integer of at least 300" >&2
  exit 2
fi

for required in \
  "${build_dir}/bin/cockpit-navigator" \
  "${build_dir}/bin/cockpit-ctl" \
  "${build_dir}/bin/camera-ctl" \
  "${build_dir}/bin/voice-ctl" \
  "${build_dir}/lib/cockpit/modules" \
  "${ai_root}/runtime/llama.cpp/current/bin/llama-server" \
  "${ai_root}/models/llm/qwen3.5-2b-q4_k_m/model.gguf" \
  "${ai_root}/config/kws-keywords.txt"; do
  if [[ ! -e "${required}" ]]; then
    echo "missing full-system soak dependency: ${required}" >&2
    exit 2
  fi
done

sed \
  -e "s|^  data_dir: .*$|  data_dir: ${run_root}/data|" \
  -e "s|^  log_dir: .*$|  log_dir: ${run_root}/logs|" \
  -e "s|^  run_dir: .*$|  run_dir: ${run_root}/run|" \
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
  -e '/^  camera:$/,/^  voice_interaction:$/s/^    capture_pipeline:.*$/    capture_pipeline: software_isp/' \
  -e '/^  bridge:$/,/^hardware:$/s/^    provider: fake$/    provider: ros2_nav2/' \
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
  -e '/^    local_llm:$/,/^tools:$/s/^      enabled: false$/      enabled: true/' \
  -e '/^    local_llm:$/,/^tools:$/s/^      provider: disabled$/      provider: llama-server/' \
  -e '/^    local_llm:$/,/^tools:$/s/^      manage_process: false$/      manage_process: true/' \
  -e "/^    local_llm:$/,/^tools:$/s|^      executable:.*$|      executable: ${ai_root}/runtime/llama.cpp/current/bin/llama-server|" \
  -e "/^    local_llm:$/,/^tools:$/s|^      model_path:.*$|      model_path: ${ai_root}/models/llm/qwen3.5-2b-q4_k_m/model.gguf|" \
  -e '/^    local_llm:$/,/^tools:$/s/^      gpu_layers:.*$/      gpu_layers: 99/' \
  "${source_config}" | \
  sed \
    -e "s|PLACEHOLDER_KEYWORDS|${ai_root}/config/kws-keywords.txt|" \
    -e "s|PLACEHOLDER_KWS_MODEL|${ai_root}/models/kws/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile|" \
    >"${config_path}"

navigator_pid=""
cleanup() {
  if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    "${build_dir}/bin/cockpit-navigator" --command shutdown --socket "${navigator_socket}" \
      >/dev/null 2>&1 || kill "${navigator_pid}" >/dev/null 2>&1 || true
    wait "${navigator_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

export COCKPIT_AI_ROOT="${ai_root}"
export LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export DISPLAY="${display}"
if [[ -f "${xauthority}" ]]; then
  export XAUTHORITY="${xauthority}"
fi

(
  cd "${run_root}"
  exec "${build_dir}/bin/cockpit-navigator" --config "${config_path}" \
    --module-dir "${build_dir}/lib/cockpit/modules" --socket "${navigator_socket}" --mode ui
) >"${navigator_log}" 2>&1 &
navigator_pid=$!

ready=false
for _ in $(seq 1 900); do
  if "${build_dir}/bin/cockpit-ctl" runtime status --socket "${navigator_socket}" 2>/dev/null | \
      grep -q 'module=hmi state=running' && \
      "${build_dir}/bin/voice-ctl" --status --config "${config_path}" >/dev/null 2>&1; then
    ready=true
    break
  fi
  if ! kill -0 "${navigator_pid}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if [[ "${ready}" != true ]]; then
  echo "full-system stack did not become ready; log: ${navigator_log}" >&2
  tail -120 "${navigator_log}" >&2 || true
  exit 1
fi

"${build_dir}/bin/camera-ctl" --start --device /dev/video0 --width 1280 --height 720 --fps 30 \
  --config "${config_path}" >/dev/null

llm_fault_at="${llm_fault_at_override:-$((duration_seconds * 60 / 100))}"
camera_fault_at="${camera_fault_at_override:-$((duration_seconds * 75 / 100))}"
python3 "${root_dir}/scripts/tests/cockpit-full-system-monitor.py" \
  --navigator-pid "${navigator_pid}" \
  --navigator-socket "${navigator_socket}" \
  --config "${config_path}" \
  --build-dir "${build_dir}" \
  --duration-seconds "${duration_seconds}" \
  --interaction-interval-seconds "${interaction_interval_seconds}" \
  --ui-click-interval-seconds 120 \
  --llm-fault-at-seconds "${llm_fault_at}" \
  --camera-fault-at-seconds "${camera_fault_at}" \
  --kws-status ENABLED_WENETSPEECH_2024_C_API_COMPATIBLE \
  --output "${raw_report}"

python3 "${root_dir}/scripts/tests/cockpit-full-system-report.py" \
  "${raw_report}" "${summary_report}"

echo "full-system soak artifacts:"
echo "  ${raw_report}"
echo "  ${summary_report}"
echo "  ${navigator_log}"
