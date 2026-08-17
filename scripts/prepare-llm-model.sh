#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
model_dir="${ai_root}/models/llm/qwen3-4b-instruct-2507-q4_k_m"
model_file="${model_dir}/model.gguf"

if [[ -f "${model_file}" ]]; then
  printf 'local LLM model already prepared: %s\n' "${model_file}"
  exit 0
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
Qwen3-4B-Instruct-2507 GGUF Q4_K_M is required. Provide one of:
  COCKPIT_LLM_MODEL_FILE=/path/to/model.gguf
  COCKPIT_LLM_MODEL_URL=https://.../model.gguf
EOF
  exit 2
fi
if [[ "${source_file}" != *.gguf ]]; then
  printf 'local LLM model must be a GGUF file: %s\n' "${source_file}" >&2
  exit 2
fi

mkdir -p "${model_dir}"
cp -f "${source_file}" "${model_file}"
{
  printf 'model=Qwen3-4B-Instruct-2507\n'
  printf 'quantization=Q4_K_M\n'
  printf 'source=%s\n' "${source_file}"
  sha256sum "${model_file}"
} >"${model_dir}/MANIFEST.txt"

printf 'local LLM model prepared: %s\n' "${model_file}"
