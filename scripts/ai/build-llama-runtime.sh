#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/../.." && pwd)"
# shellcheck source=scripts/ai/llm-versions.sh
source "${script_dir}/llm-versions.sh"

jobs="${JOBS:-2}"
build_ui="${COCKPIT_LLAMA_CPP_BUILD_UI:-OFF}"
use_prebuilt_ui="${COCKPIT_LLAMA_CPP_USE_PREBUILT_UI:-OFF}"
if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "JOBS must be a positive integer" >&2
  exit 2
fi

native_arch="$(uname -m)"
case "${native_arch}" in
  aarch64|arm64)
    cuda="${COCKPIT_LLAMA_CPP_CUDA:-ON}"
    default_cuda_architectures=87
    ;;
  x86_64|amd64)
    cuda="${COCKPIT_LLAMA_CPP_CUDA:-OFF}"
    default_cuda_architectures=""
    ;;
  *)
    echo "unsupported native architecture: ${native_arch}" >&2
    exit 2
    ;;
esac

if [[ "${cuda}" == "ON" ]]; then
  cuda_architectures="${COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES:-${default_cuda_architectures}}"
else
  cuda_architectures=""
fi

ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
source_dir="${COCKPIT_LLAMA_CPP_SOURCE_DIR:-${ai_root}/sources/llama.cpp/${LLAMA_CPP_REVISION}}"
runtime_dir="${ai_root}/runtime/llama.cpp/${LLAMA_CPP_REVISION}"
server_bin="${runtime_dir}/bin/llama-server"
manifest="${runtime_dir}/MANIFEST.txt"
model_file="${ai_root}/models/llm/qwen3.5-2b-q4_k_m/model.gguf"

print_command() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
}

run() {
  print_command "$@"
  "$@"
}

if [[ ! -d "${source_dir}/.git" ]]; then
  echo "reviewed llama.cpp checkout is missing: ${source_dir}" >&2
  echo "prepare it first with scripts/ai/bootstrap-llm.sh or set COCKPIT_LLAMA_CPP_SOURCE_DIR" >&2
  exit 2
fi

actual_revision="$(git -C "${source_dir}" rev-parse HEAD)"
if [[ "${actual_revision}" != "${LLAMA_CPP_REVISION}" ]]; then
  echo "llama.cpp revision mismatch: expected ${LLAMA_CPP_REVISION}, got ${actual_revision}" >&2
  exit 1
fi

printf 'llama.cpp native build\n'
printf '  arch=%s\n' "${native_arch}"
printf '  revision=%s\n' "${LLAMA_CPP_REVISION}"
printf '  cuda=%s\n' "${cuda}"
printf '  cuda_architectures=%s\n' "${cuda_architectures}"
printf '  jobs=%s\n' "${jobs}"
printf '  build_ui=%s\n' "${build_ui}"
printf '  use_prebuilt_ui=%s\n' "${use_prebuilt_ui}"
printf '  source=%s\n' "${source_dir}"
printf '  runtime=%s\n' "${runtime_dir}"

run env \
  COCKPIT_AI_ROOT="${ai_root}" \
  COCKPIT_LLAMA_CPP_REVISION="${LLAMA_CPP_REVISION}" \
  COCKPIT_LLAMA_CPP_SOURCE_DIR="${source_dir}" \
  COCKPIT_LLAMA_CPP_CUDA="${cuda}" \
  COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES="${cuda_architectures}" \
  COCKPIT_LLAMA_CPP_BUILD_UI="${build_ui}" \
  COCKPIT_LLAMA_CPP_USE_PREBUILT_UI="${use_prebuilt_ui}" \
  JOBS="${jobs}" \
  bash "${script_dir}/prepare-llama-runtime.sh"

[[ -x "${server_bin}" ]] || {
  echo "llama-server was not installed: ${server_bin}" >&2
  exit 1
}
[[ -f "${manifest}" ]] || {
  echo "llama runtime manifest is missing: ${manifest}" >&2
  exit 1
}

run cat "${manifest}"
grep -Fxq "revision=${LLAMA_CPP_REVISION}" "${manifest}"
grep -Fxq "arch=${native_arch}" "${manifest}"
grep -Fxq "cuda=${cuda}" "${manifest}"
grep -Fxq "cuda_architectures=${cuda_architectures}" "${manifest}"
grep -Fxq "build_ui=${build_ui}" "${manifest}"
grep -Fxq "use_prebuilt_ui=${use_prebuilt_ui}" "${manifest}"

run file "${server_bin}"
print_command env "LD_LIBRARY_PATH=${runtime_dir}/bin${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ldd "${server_bin}"
ldd_output="$(LD_LIBRARY_PATH="${runtime_dir}/bin${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ldd "${server_bin}")"
printf '%s\n' "${ldd_output}"
if grep -Fq 'not found' <<<"${ldd_output}"; then
  echo "llama-server has unresolved dynamic dependencies" >&2
  exit 1
fi

run env "LD_LIBRARY_PATH=${runtime_dir}/bin${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${server_bin}" --version

if [[ -f "${model_file}" ]]; then
  print_command sha256sum "${model_file}"
  actual_model_sha256="$(sha256sum "${model_file}" | awk '{print $1}')"
  printf '%s  %s\n' "${actual_model_sha256}" "${model_file}"
  if [[ "${actual_model_sha256}" != "${QWEN_2B_SHA256}" ]]; then
    echo "Qwen3.5-2B model SHA-256 mismatch" >&2
    exit 1
  fi
else
  echo "INFO Qwen3.5-2B model is not present; runtime checks passed without model verification"
fi

echo "llama.cpp native runtime build and checks passed"
