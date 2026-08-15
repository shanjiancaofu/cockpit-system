#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
version="${COCKPIT_SHERPA_ONNX_VERSION:-v1.13.4}"
runtime_dir="${ai_root}/runtime/sherpa-onnx/${version}"

required_header="${runtime_dir}/include/sherpa-onnx/c-api/c-api.h"
required_lib="${runtime_dir}/lib/libsherpa-onnx-c-api.so"

if [[ -f "${required_header}" && -f "${required_lib}" ]]; then
  printf 'Sherpa-ONNX runtime already prepared: %s\n' "${runtime_dir}"
  exit 0
fi

mkdir -p "${runtime_dir}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${tmp_dir}"' EXIT

archive="${COCKPIT_SHERPA_RUNTIME_ARCHIVE:-}"
url="${COCKPIT_SHERPA_RUNTIME_URL:-}"
if [[ -z "${archive}" && -n "${url}" ]]; then
  archive="${tmp_dir}/sherpa-onnx-runtime.tar.gz"
  curl -fL --retry 3 --connect-timeout 20 -o "${archive}" "${url}"
fi

if [[ -z "${archive}" ]]; then
  cat >&2 <<EOF
Sherpa-ONNX runtime is missing.

Expected:
  ${required_header}
  ${required_lib}

Provide one of:
  COCKPIT_SHERPA_RUNTIME_ARCHIVE=/path/to/runtime.tar.gz
  COCKPIT_SHERPA_RUNTIME_URL=https://...

Target root:
  ${runtime_dir}
EOF
  exit 1
fi

case "${archive}" in
  *.tar|*.tar.gz|*.tgz|*.tar.xz)
    tar -xf "${archive}" -C "${tmp_dir}"
    ;;
  *.zip)
    unzip -q "${archive}" -d "${tmp_dir}"
    ;;
  *)
    printf 'unsupported Sherpa runtime archive: %s\n' "${archive}" >&2
    exit 1
    ;;
esac

source_dir="$(find "${tmp_dir}" -mindepth 1 -maxdepth 2 -type d -name include -print -quit)"
if [[ -z "${source_dir}" ]]; then
  printf 'archive does not contain include/\n' >&2
  exit 1
fi
archive_root="$(dirname "${source_dir}")"
if [[ ! -d "${archive_root}/lib" ]]; then
  printf 'archive does not contain lib/\n' >&2
  exit 1
fi

cp -a "${archive_root}/include" "${runtime_dir}/"
cp -a "${archive_root}/lib" "${runtime_dir}/"

if [[ ! -f "${required_header}" || ! -f "${required_lib}" ]]; then
  printf 'Sherpa runtime archive did not produce required files under %s\n' "${runtime_dir}" >&2
  exit 1
fi

{
  printf 'version=%s\n' "${version}"
  printf 'source_archive=%s\n' "${archive}"
  sha256sum "${archive}" 2>/dev/null || true
} >"${runtime_dir}/MANIFEST.txt"

printf 'Sherpa-ONNX runtime prepared: %s\n' "${runtime_dir}"
