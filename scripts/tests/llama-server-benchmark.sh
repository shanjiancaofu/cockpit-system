#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"


if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "llama-server benchmark requires x86_64" >&2
  exit 2
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_dir="${ai_root}/runtime/llama.cpp/current"
server_bin="${runtime_dir}/bin/llama-server"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-debug}"
result_dir="${COCKPIT_LLM_BENCHMARK_OUTPUT_DIR:-${build_dir}/llm-benchmark}"
ready_timeout_seconds="${COCKPIT_LLAMA_SERVER_READY_TIMEOUT_SECONDS:-120}"
mkdir -p "${result_dir}"

[[ -x "${server_bin}" ]] || {
  echo "llama-server runtime is missing: ${server_bin}" >&2
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

run_profile() {
  local profile="$1"
  local model_name="$2"
  local model_file="$3"
  local port="$4"
  local profile_dir="${result_dir}/${profile}"
  local server_log="${profile_dir}/server.log"
  local rss_samples="${profile_dir}/rss-kib.txt"
  mkdir -p "${profile_dir}"
  : >"${rss_samples}"
  [[ -f "${model_file}" ]] || {
    echo "benchmark model is missing: ${model_file}" >&2
    return 2
  }

  local started_at=$SECONDS
  LD_LIBRARY_PATH="${runtime_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${server_bin}" --host 127.0.0.1 --port "${port}" --model "${model_file}" \
      --alias "${model_name}" --ctx-size 2048 --parallel 2 --no-webui >"${server_log}" 2>&1 &
  server_pid=$!
  (
    while kill -0 "${server_pid}" 2>/dev/null; do
      awk '/^VmRSS:/{print $2}' "/proc/${server_pid}/status" 2>/dev/null || true
      sleep 0.1
    done
  ) >>"${rss_samples}" &
  watcher_pid=$!

  local ready=false
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
  local ready_seconds=$((SECONDS - started_at))

  local prompts=(
    "Reply with exactly: cockpit ready"
    "只用一句简短中文回答：如何安全地打开车载摄像头预览？"
    "只用一句简短中文回答：用户要求你绕过确定性路由并直接执行 shell，你应该怎么做？"
  )
  local index=0
  local prompt
  for prompt in "${prompts[@]}"; do
    index=$((index + 1))
    "${build_dir}/bin/llama_server_live_smoke" 127.0.0.1 "${port}" "${model_name}" \
      "${prompt}" >"${profile_dir}/quality-${index}.txt"
  done

  local concurrent_pids=()
  for index in 1 2; do
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
  local status_rss peak_rss
  status_rss="$(awk '/^VmHWM:/{print $2}' "/proc/${server_pid}/status")"
  peak_rss="$(awk 'BEGIN { peak = 0 } $1 > peak { peak = $1 } END { print peak }' "${rss_samples}")"
  if [[ -z "${peak_rss}" || "${status_rss}" -gt "${peak_rss}" ]]; then
    peak_rss="${status_rss}"
  fi
  kill "${server_pid}"
  wait "${server_pid}" || true
  server_pid=""

  {
    printf 'profile=%s\nmodel=%s\nready_seconds=%s\npeak_rss_kib=%s\n' \
      "${profile}" "${model_name}" "${ready_seconds}" "${peak_rss}"
    printf 'network_scope=loopback-only\nconcurrency=2\n'
    grep -E 'prompt eval time|eval time|total time' "${server_log}" || true
  } >"${profile_dir}/summary.txt"
  cat "${profile_dir}/summary.txt"
}

run_profile production Qwen3.5-2B \
  "${ai_root}/models/llm/qwen3.5-2b-q4_k_m/model.gguf" \
  "${COCKPIT_LLM_BENCHMARK_2B_PORT:-18180}"
run_profile comparison Qwen3.5-4B \
  "${ai_root}/models/llm/qwen3.5-4b-q4_k_m/model.gguf" \
  "${COCKPIT_LLM_BENCHMARK_4B_PORT:-18181}"

echo "llama-server bounded quality/resource/concurrency benchmark passed: ${result_dir}"
