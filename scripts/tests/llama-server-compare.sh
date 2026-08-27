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
