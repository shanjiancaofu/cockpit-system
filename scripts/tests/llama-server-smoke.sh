#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
revision="${COCKPIT_LLAMA_CPP_REVISION:-}"
if [[ -z "${revision}" ]]; then
  echo "COCKPIT_LLAMA_CPP_REVISION is required" >&2
  exit 2
fi

server_bin="${COCKPIT_LLAMA_SERVER_BIN:-${ai_root}/runtime/llama.cpp/${revision}/bin/llama-server}"
model_file="${COCKPIT_LLM_MODEL_FILE:-${ai_root}/models/llm/qwen3-4b-instruct-2507-q4_k_m/model.gguf}"
port="${COCKPIT_LLAMA_SERVER_PORT:-18080}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-debug}"
log_file="${build_dir}/llama-server-smoke.log"

for required in "${server_bin}" "${model_file}"; do
  if [[ ! -f "${required}" ]]; then
    printf 'required local LLM resource is missing: %s\n' "${required}" >&2
    exit 2
  fi
done

mkdir -p "${build_dir}"
LD_LIBRARY_PATH="$(dirname "${server_bin}")${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${server_bin}" --host 127.0.0.1 --port "${port}" --model "${model_file}" \
    --ctx-size 2048 --parallel 1 --no-webui >"${log_file}" 2>&1 &
server_pid=$!
cleanup() {
  kill "${server_pid}" 2>/dev/null || true
  wait "${server_pid}" 2>/dev/null || true
}
trap cleanup EXIT

ready=false
for _ in $(seq 1 120); do
  if ! kill -0 "${server_pid}" 2>/dev/null; then
    echo "llama-server exited during startup" >&2
    tail -100 "${log_file}" >&2
    exit 1
  fi
  if curl -fsS --max-time 1 "http://127.0.0.1:${port}/health" >/dev/null; then
    ready=true
    break
  fi
  sleep 0.25
done
if [[ "${ready}" != true ]]; then
  echo "llama-server did not become ready" >&2
  tail -100 "${log_file}" >&2
  exit 1
fi

cmake --build "${build_dir}" --target llama_server_live_smoke --parallel "${JOBS:-$(nproc)}"
"${build_dir}/bin/llama_server_live_smoke" 127.0.0.1 "${port}"
echo "llama-server live smoke passed"
