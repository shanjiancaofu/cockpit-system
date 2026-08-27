#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
profile="${COCKPIT_LLM_MODEL_PROFILE:-production}"
case "${profile}" in
  production)
    model_name="Qwen3.5-2B"
    model_slug="qwen3.5-2b-q4_k_m"
    ;;
  comparison)
    model_name="Qwen3.5-4B"
    model_slug="qwen3.5-4b-q4_k_m"
    ;;
  *)
    echo "COCKPIT_LLM_MODEL_PROFILE must be production or comparison" >&2
    exit 2
    ;;
esac
model_dir="${ai_root}/models/llm/${model_slug}"
model_file="${model_dir}/model.gguf"
expected_sha256="${COCKPIT_LLM_MODEL_SHA256:-}"

if [[ ! "${expected_sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
  echo "COCKPIT_LLM_MODEL_SHA256 must be the reviewed GGUF SHA-256" >&2
  exit 2
fi

source_file="${COCKPIT_LLM_MODEL_FILE:-}"
source_url="${COCKPIT_LLM_MODEL_URL:-}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${tmp_dir}"' EXIT
if [[ -z "${source_file}" && -n "${source_url}" ]]; then
  source_file="${tmp_dir}/model.gguf"
  curl -fL --retry 3 --connect-timeout 20 -o "${source_file}" "${source_url}"
fi
if [[ -z "${source_file}" || ! -f "${source_file}" ]]; then
  cat >&2 <<EOF
${model_name} GGUF Q4_K_M is required. Provide one of:
  COCKPIT_LLM_MODEL_FILE=/path/to/model.gguf
  COCKPIT_LLM_MODEL_URL=https://.../model.gguf
EOF
  exit 2
fi
if [[ "${source_file}" != *.gguf ]]; then
  printf 'local LLM model must be a GGUF file: %s\n' "${source_file}" >&2
  exit 2
fi
printf '%s  %s\n' "${expected_sha256}" "${source_file}" | sha256sum --check --status || {
  echo "local LLM model SHA-256 verification failed" >&2
  exit 1
}

mkdir -p "${model_dir}"
if [[ -f "${model_file}" ]] &&
   printf '%s  %s\n' "${expected_sha256}" "${model_file}" | sha256sum --check --status; then
  printf 'local LLM model already prepared: %s\n' "${model_file}"
  exit 0
fi
if [[ -e "${model_file}" ]]; then
  printf 'existing local LLM model SHA-256 verification failed; replacing from verified source: %s\n' \
    "${model_file}" >&2
fi

tmp_model="$(mktemp "${model_dir}/.model.gguf.tmp.XXXXXX")"
trap 'rm -rf --one-file-system "${tmp_dir}"; rm -f "${tmp_model:-}"' EXIT
cp "${source_file}" "${tmp_model}"
printf '%s  %s\n' "${expected_sha256}" "${tmp_model}" | sha256sum --check --status || {
  echo "copied local LLM model SHA-256 verification failed" >&2
  exit 1
}
mv -f "${tmp_model}" "${model_file}"
tmp_model=""
{
  printf 'profile=%s\n' "${profile}"
  printf 'model=%s\n' "${model_name}"
  printf 'quantization=Q4_K_M\n'
  printf 'source=%s\n' "${source_file}"
  printf 'sha256=%s\n' "${expected_sha256,,}"
} >"${model_dir}/MANIFEST.txt"

printf 'local LLM model prepared: %s\n' "${model_file}"
