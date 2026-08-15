#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"

kws_dir="${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20"
vad_dir="${ai_root}/models/vad/silero-vad"
asr_dir="${ai_root}/models/asr/sensevoice-small-int8"
config_dir="${ai_root}/config"

extract_archive() {
  local archive="$1"
  local target="$2"
  mkdir -p "${target}"
  case "${archive}" in
    *.tar|*.tar.gz|*.tgz|*.tar.xz)
      tar -xf "${archive}" -C "${target}" --strip-components=1
      ;;
    *.zip)
      unzip -q "${archive}" -d "${target}"
      ;;
    *)
      printf 'unsupported model archive: %s\n' "${archive}" >&2
      return 1
      ;;
  esac
}

fetch_archive() {
  local url="$1"
  local output="$2"
  curl -fL --retry 3 --connect-timeout 20 -o "${output}" "${url}"
}

prepare_model() {
  local name="$1"
  local target="$2"
  local archive_env="$3"
  local url_env="$4"
  shift 4
  local missing=()
  for required in "$@"; do
    if [[ ! -f "${target}/${required}" ]]; then
      missing+=("${target}/${required}")
    fi
  done
  if [[ ${#missing[@]} -eq 0 ]]; then
    printf '%s model already prepared: %s\n' "${name}" "${target}"
    return 0
  fi

  local archive="${!archive_env:-}"
  local url="${!url_env:-}"
  local tmp_dir
  tmp_dir="$(mktemp -d)"
  trap 'rm -rf --one-file-system "${tmp_dir}"' RETURN
  if [[ -z "${archive}" && -n "${url}" ]]; then
    archive="${tmp_dir}/${name}.archive"
    fetch_archive "${url}" "${archive}"
  fi
  if [[ -n "${archive}" ]]; then
    extract_archive "${archive}" "${target}"
  fi

  missing=()
  for required in "$@"; do
    if [[ ! -f "${target}/${required}" ]]; then
      missing+=("${target}/${required}")
    fi
  done
  if [[ ${#missing[@]} -ne 0 ]]; then
    printf '%s model is missing required files:\n' "${name}" >&2
    printf '  %s\n' "${missing[@]}" >&2
    printf 'Provide %s or %s, or place files under %s\n' "${archive_env}" "${url_env}" \
      "${target}" >&2
    return 1
  fi

  {
    printf 'name=%s\n' "${name}"
    printf 'source_archive=%s\n' "${archive:-manual}"
    [[ -n "${archive}" ]] && sha256sum "${archive}" 2>/dev/null || true
  } >"${target}/MANIFEST.txt"
  printf '%s model prepared: %s\n' "${name}" "${target}"
}

mkdir -p "${config_dir}"
if [[ ! -f "${config_dir}/kws-keywords-raw.txt" ]]; then
  printf '你好小车\n' >"${config_dir}/kws-keywords-raw.txt"
fi

prepare_model "kws" "${kws_dir}" COCKPIT_KWS_MODEL_ARCHIVE COCKPIT_KWS_MODEL_URL \
  encoder-epoch-13-avg-2-chunk-8-left-64.int8.onnx \
  decoder-epoch-13-avg-2-chunk-8-left-64.onnx \
  joiner-epoch-13-avg-2-chunk-8-left-64.int8.onnx \
  tokens.txt

prepare_model "vad" "${vad_dir}" COCKPIT_VAD_MODEL_ARCHIVE COCKPIT_VAD_MODEL_URL \
  silero_vad.onnx

prepare_model "asr" "${asr_dir}" COCKPIT_ASR_MODEL_ARCHIVE COCKPIT_ASR_MODEL_URL \
  model.int8.onnx \
  tokens.txt

if [[ ! -f "${config_dir}/kws-keywords.txt" ]]; then
  cat >&2 <<EOF
Tokenized KWS keywords file is missing:
  ${config_dir}/kws-keywords.txt

Create it from:
  ${config_dir}/kws-keywords-raw.txt

Use the matching Sherpa text2token tool for the prepared KWS model; do not pass
raw Chinese wake words directly to the C++ runtime.
EOF
  exit 1
fi

printf 'Voice AI models prepared under %s\n' "${ai_root}"
