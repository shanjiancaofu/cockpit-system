#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  printf 'refusing to run as root; do not use sudo\n' >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
model_dir="${ai_root}/models/tts/kokoro-multi-lang-v1_1"
downloads_dir="${ai_root}/downloads"
archive="${COCKPIT_KOKORO_TTS_ARCHIVE:-${downloads_dir}/kokoro-multi-lang-v1_1.tar.bz2}"
url="${COCKPIT_KOKORO_TTS_URL:-https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2}"

# This is the SHA-256 of the official Sherpa-ONNX v1.1 Chinese/English package.
expected_sha256="${COCKPIT_KOKORO_TTS_SHA256:-a3f4c73d043860e3fd2e5b06f36795eb81de0fc8e8de6df703245edddd87dbad}"
required=(model.onnx voices.bin tokens.txt lexicon-us-en.txt lexicon-zh.txt espeak-ng-data)

has_model() {
  local item
  for item in "${required[@]}"; do
    if [[ ! -e "${model_dir}/${item}" ]]; then
      return 1
    fi
  done
}

mkdir -p "${downloads_dir}" "${model_dir}"
if [[ ! -f "${archive}" ]]; then
  curl -fL --retry 3 --connect-timeout 20 --continue-at - --progress-bar -o "${archive}" "${url}"
fi
printf '%s  %s\n' "${expected_sha256}" "${archive}" | sha256sum --check --strict

if has_model; then
  printf 'Kokoro TTS model already prepared: %s\n' "${model_dir}"
  exit 0
fi

tmp_dir="$(mktemp -d "${model_dir}.tmp.XXXXXX")"
cleanup() { rm -rf --one-file-system "${tmp_dir}"; }
trap cleanup EXIT
tar -xf "${archive}" -C "${tmp_dir}" --strip-components=1
for item in "${required[@]}"; do
  [[ -e "${tmp_dir}/${item}" ]] || {
    printf 'Kokoro archive is missing %s\n' "${item}" >&2
    exit 1
  }
done
rm -rf --one-file-system "${model_dir}"
mv "${tmp_dir}" "${model_dir}"
trap - EXIT
printf 'Kokoro TTS model prepared: %s\n' "${model_dir}"
