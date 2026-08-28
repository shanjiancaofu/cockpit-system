#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"


if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "llama-server comparison requires x86_64" >&2
  exit 2
fi

build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-debug}"
result_dir="${COCKPIT_LLM_COMPARE_OUTPUT_DIR:-${build_dir}/llm-compare}"
mkdir -p "${result_dir}"

for profile in production comparison; do
  output="${result_dir}/${profile}.txt"
  timing="${result_dir}/${profile}.time"
  echo "[$profile] running local llama-server smoke (existing resources only)"
  {
    /usr/bin/time -f 'elapsed_seconds=%e\nmax_rss_kb=%M' -o "${timing}" \
      env COCKPIT_LLM_MODEL_PROFILE="${profile}" \
      BUILD_DIR="${build_dir}" \
      bash "${root_dir}/scripts/tests/llama-server-smoke.sh"
  } 2>&1 | tee "${output}"
  server_log="${build_dir}/llama-server-${profile}-smoke.log"
  {
    cat "${timing}"
    if [[ -f "${server_log}" ]]; then
      grep -E 'model loaded|prompt eval time|eval time|total time' "${server_log}" || true
    fi
  } | tee -a "${output}"
  printf 'result_file=%s\n' "${output}"
done

echo "llama-server 2B/4B comparison completed; results are under ${result_dir}"
