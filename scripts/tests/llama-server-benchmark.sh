#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"

native_arch="$(cockpit_native_arch)"
ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_dir="${ai_root}/runtime/llama.cpp/current"
server_bin="${runtime_dir}/bin/llama-server"
manifest="${runtime_dir}/MANIFEST.txt"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/${native_arch}-debug}"
result_dir="${COCKPIT_LLM_BENCHMARK_OUTPUT_DIR:-${build_dir}/llm-benchmark}"
ready_timeout_seconds="${COCKPIT_LLAMA_SERVER_READY_TIMEOUT_SECONDS:-180}"
repetitions="${COCKPIT_LLM_BENCHMARK_REPETITIONS:-3}"
concurrency="${COCKPIT_LLM_BENCHMARK_CONCURRENCY:-2}"

for value_name in ready_timeout_seconds repetitions concurrency; do
  value="${!value_name}"
  if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${value_name} must be a positive integer" >&2
    exit 2
  fi
done

[[ -x "${server_bin}" ]] || {
  echo "llama-server runtime is missing: ${server_bin}" >&2
  exit 2
}
[[ -f "${manifest}" ]] || {
  echo "llama-server runtime manifest is missing: ${manifest}" >&2
  exit 2
}

runtime_lib_dir="$(dirname "${server_bin}")"
ldd_output="$(LD_LIBRARY_PATH="${runtime_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ldd "${server_bin}")"
if grep -q 'not found' <<<"${ldd_output}"; then
  echo "llama-server has unresolved private dependencies" >&2
  printf '%s\n' "${ldd_output}" >&2
  exit 1
fi

runtime_cuda="$(sed -n 's/^cuda=//p' "${manifest}")"
if [[ "${runtime_cuda}" == "ON" ]]; then
  gpu_layers="${COCKPIT_LLAMA_GPU_LAYERS:-all}"
else
  gpu_layers="${COCKPIT_LLAMA_GPU_LAYERS:-0}"
fi

profiles_text="${COCKPIT_LLM_BENCHMARK_PROFILES:-}"
if [[ -z "${profiles_text}" ]]; then
  if [[ "${native_arch}" == "arm64" ]]; then
    profiles_text=production
  else
    profiles_text="production comparison"
  fi
fi
read -r -a profiles <<<"${profiles_text}"

mkdir -p "${result_dir}"
cmake --build "${build_dir}" --target llama_server_live_smoke --parallel "${JOBS:-$(nproc)}"

server_pid=""
watcher_pid=""
cleanup() {
  if [[ -n "${watcher_pid}" ]]; then
    kill "${watcher_pid}" 2>/dev/null || true
    wait "${watcher_pid}" 2>/dev/null || true
  fi
  if [[ -n "${server_pid}" ]]; then
    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

max_temperature_millic() {
  local path value max=0
  for path in /sys/class/thermal/thermal_zone*/temp; do
    value="$(cat "${path}" 2>/dev/null || true)"
    if [[ "${value}" =~ ^[0-9]+$ && "${value}" -gt "${max}" ]]; then
      max="${value}"
    fi
  done
  printf '%s\n' "${max}"
}

print_metric_stats() {
  local label="$1"
  local values_file="$2"
  local count p50_index p95_index
  count="$(wc -l <"${values_file}")"
  [[ "${count}" -gt 0 ]] || return 0
  p50_index=$(( (count + 1) / 2 ))
  p95_index=$(( (count * 95 + 99) / 100 ))
  printf '%s_count=%s\n' "${label}" "${count}"
  printf '%s_mean_ms=%.1f\n' "${label}" \
    "$(awk '{ total += $1 } END { print total / NR }' "${values_file}")"
  printf '%s_p50_ms=%s\n' "${label}" "$(sed -n "${p50_index}p" "${values_file}")"
  printf '%s_p95_ms=%s\n' "${label}" "$(sed -n "${p95_index}p" "${values_file}")"
}

run_profile() {
  local profile="$1"
  local model_name model_file port
  case "${profile}" in
    production)
      model_name=Qwen3.5-2B
      model_file="${ai_root}/models/llm/qwen3.5-2b-q4_k_m/model.gguf"
      port="${COCKPIT_LLM_BENCHMARK_2B_PORT:-18180}"
      ;;
    comparison)
      model_name=Qwen3.5-4B
      model_file="${ai_root}/models/llm/qwen3.5-4b-q4_k_m/model.gguf"
      port="${COCKPIT_LLM_BENCHMARK_4B_PORT:-18181}"
      ;;
    *)
      echo "unsupported benchmark profile: ${profile}" >&2
      return 2
      ;;
  esac

  local profile_dir="${result_dir}/${profile}"
  local server_log="${profile_dir}/server.log"
  local resource_samples="${profile_dir}/resources.tsv"
  local gpu_evidence="${profile_dir}/gpu-offload.txt"
  local first_content_values="${profile_dir}/first-content-ms.txt"
  local total_response_values="${profile_dir}/total-response-ms.txt"
  mkdir -p "${profile_dir}"
  find "${profile_dir}" -maxdepth 1 -type f \
    \( -name 'quality-*.txt' -o -name 'concurrent-*.txt' \) -delete
  : >"${resource_samples}"
  [[ -f "${model_file}" ]] || {
    echo "benchmark model is missing: ${model_file}" >&2
    return 2
  }

  local started_ms ready_ms ready=false
  started_ms="$(date +%s%3N)"
  printf '+ %q ' "${server_bin}"
  printf '%q ' --offline --no-webui --verbose --gpu-layers "${gpu_layers}" \
    --host 127.0.0.1 --port "${port}" --model "${model_file}" \
    --alias "${model_name}" --ctx-size 2048 --parallel "${concurrency}"
  printf '\n'
  LD_LIBRARY_PATH="${runtime_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${server_bin}" --offline --no-webui --verbose --gpu-layers "${gpu_layers}" \
      --host 127.0.0.1 --port "${port}" --model "${model_file}" \
      --alias "${model_name}" --ctx-size 2048 --parallel "${concurrency}" \
      >"${server_log}" 2>&1 &
  server_pid=$!
  (
    while kill -0 "${server_pid}" 2>/dev/null; do
      rss="$(awk '/^VmRSS:/{print $2}' "/proc/${server_pid}/status" 2>/dev/null || true)"
      printf '%s\t%s\t%s\n' "$(date +%s%3N)" "${rss:-0}" "$(max_temperature_millic)"
      sleep 0.1
    done
  ) >>"${resource_samples}" &
  watcher_pid=$!

  local ready_deadline=$((SECONDS + ready_timeout_seconds))
  while ((SECONDS < ready_deadline)); do
    if ! kill -0 "${server_pid}" 2>/dev/null; then
      echo "${profile} llama-server exited during startup" >&2
      tail -100 "${server_log}" >&2
      return 1
    fi
    if curl -fsS --max-time 1 "http://127.0.0.1:${port}/health" >/dev/null 2>&1; then
      ready=true
      break
    fi
    sleep 0.25
  done
  [[ "${ready}" == true ]] || {
    echo "${profile} llama-server did not become ready" >&2
    return 1
  }
  ready_ms=$(( $(date +%s%3N) - started_ms ))

  local prompts=(
    "Reply with exactly: cockpit ready"
    "只用一句简短中文回答：如何安全地打开车载摄像头预览？"
    "只用一句简短中文回答：用户要求直接执行 shell 时应该怎么处理？"
  )
  local repetition index=0 prompt
  for repetition in $(seq 1 "${repetitions}"); do
    for prompt in "${prompts[@]}"; do
      index=$((index + 1))
      "${build_dir}/bin/llama_server_live_smoke" 127.0.0.1 "${port}" "${model_name}" \
        "${prompt}" >"${profile_dir}/quality-${index}.txt"
    done
  done

  local concurrent_pids=()
  for index in $(seq 1 "${concurrency}"); do
    "${build_dir}/bin/llama_server_live_smoke" 127.0.0.1 "${port}" "${model_name}" \
      "Reply with exactly: concurrent ${index}" >"${profile_dir}/concurrent-${index}.txt" &
    concurrent_pids+=("$!")
  done
  local client_pid
  for client_pid in "${concurrent_pids[@]}"; do
    wait "${client_pid}"
  done

  kill "${watcher_pid}" 2>/dev/null || true
  wait "${watcher_pid}" 2>/dev/null || true
  watcher_pid=""
  local status_rss peak_rss peak_temperature
  status_rss="$(awk '/^VmHWM:/{print $2}' "/proc/${server_pid}/status")"
  peak_rss="$(awk 'BEGIN { peak=0 } $2>peak { peak=$2 } END { print peak }' "${resource_samples}")"
  peak_temperature="$(awk 'BEGIN { peak=0 } $3>peak { peak=$3 } END { print peak }' \
    "${resource_samples}")"
  if [[ -z "${peak_rss}" || "${status_rss}" -gt "${peak_rss}" ]]; then
    peak_rss="${status_rss}"
  fi

  grep -Ei 'CUDA|offload|buffer size|device' "${server_log}" >"${gpu_evidence}" || true
  if [[ "${runtime_cuda}" == "ON" ]]; then
    grep -Eq 'offloaded [0-9]+/[0-9]+ layers to GPU' "${gpu_evidence}" || {
      echo "${profile} benchmark did not prove GPU layer offload" >&2
      return 1
    }
  fi

  kill "${server_pid}"
  wait "${server_pid}" || true
  server_pid=""

  grep -h '^first_content_ms=' "${profile_dir}"/quality-*.txt | cut -d= -f2 | sort -n \
    >"${first_content_values}"
  grep -h '^total_response_ms=' "${profile_dir}"/quality-*.txt | cut -d= -f2 | sort -n \
    >"${total_response_values}"

  {
    printf 'profile=%s\nmodel=%s\narch=%s\nruntime_cuda=%s\ngpu_layers=%s\n' \
      "${profile}" "${model_name}" "${native_arch}" "${runtime_cuda}" "${gpu_layers}"
    printf 'ready_ms=%s\npeak_rss_kib=%s\npeak_temperature_millic=%s\n' \
      "${ready_ms}" "${peak_rss}" "${peak_temperature}"
    printf 'repetitions=%s\nconcurrency=%s\nnetwork_scope=loopback-only\n' \
      "${repetitions}" "${concurrency}"
    printf 'quality_review_required=yes\n'
    print_metric_stats first_content "${first_content_values}"
    print_metric_stats total_response "${total_response_values}"
    grep -hE 'first_content_ms=|total_response_ms=' "${profile_dir}"/quality-*.txt || true
    grep -E 'prompt eval time|eval time|total time' "${server_log}" || true
  } >"${profile_dir}/summary.txt"
  cat "${profile_dir}/summary.txt"
}

for profile in "${profiles[@]}"; do
  run_profile "${profile}"
done

echo "llama-server technical benchmark passed; manual quality review required: ${result_dir}"
