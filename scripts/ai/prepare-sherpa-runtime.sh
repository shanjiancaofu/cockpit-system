#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  echo "refusing to run as root; do not use sudo" >&2
  exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
version="${COCKPIT_SHERPA_ONNX_VERSION:-v1.13.4}"
expected_sha256="${COCKPIT_SHERPA_RUNTIME_SHA256:-}"
runtime_root="${ai_root}/runtime/sherpa-onnx"
runtime_dir="${runtime_root}/${version}"

if [[ ! "${version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "COCKPIT_SHERPA_ONNX_VERSION must be a pinned vMAJOR.MINOR.PATCH version" >&2
  exit 2
fi

validate_runtime() {
  local root="$1"
  local required=(
    include/sherpa-onnx/c-api/c-api.h
    lib/libsherpa-onnx-c-api.so
    lib/libsherpa-onnx-cxx-api.so
    lib/libonnxruntime.so
    LICENSE
    MANIFEST.txt
  )
  local item
  for item in "${required[@]}"; do
    [[ -f "${root}/${item}" ]] || {
      echo "Sherpa runtime is missing ${item}: ${root}" >&2
      return 1
    }
  done
  [[ -s "${root}/LICENSE" ]] || {
    echo "Sherpa runtime LICENSE is empty" >&2
    return 1
  }
  grep -Fxq "version=${version}" "${root}/MANIFEST.txt" || {
    echo "Sherpa runtime manifest version does not match ${version}" >&2
    return 1
  }
  grep -Eq '^archive_sha256=[0-9a-f]{64}$' "${root}/MANIFEST.txt" || {
    echo "Sherpa runtime manifest does not contain a pinned archive SHA-256" >&2
    return 1
  }
  local license_sha256
  license_sha256="$(sha256sum "${root}/LICENSE" | awk '{print $1}')"
  grep -Fxq "license_sha256=${license_sha256}" "${root}/MANIFEST.txt" || {
    echo "Sherpa runtime LICENSE does not match its manifest" >&2
    return 1
  }
  local ldd_output
  if ! ldd_output="$(LD_LIBRARY_PATH="${root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
      ldd "${root}/lib/libsherpa-onnx-c-api.so")"; then
    echo "failed to inspect Sherpa runtime dynamic dependencies" >&2
    return 1
  fi
  if grep -q 'not found' <<<"${ldd_output}"; then
    echo "Sherpa runtime has unresolved private dependencies:" >&2
    printf '%s\n' "${ldd_output}" >&2
    return 1
  fi
}

if [[ -d "${runtime_dir}" ]] && validate_runtime "${runtime_dir}"; then
  mkdir -p "${runtime_root}"
  temporary_link="${runtime_root}/.current.new.$$"
  ln -s "${version}" "${temporary_link}"
  mv -Tf "${temporary_link}" "${runtime_root}/current"
  printf 'Sherpa-ONNX runtime already prepared: %s\n' "${runtime_dir}"
  exit 0
fi

archive="${COCKPIT_SHERPA_RUNTIME_ARCHIVE:-}"
url="${COCKPIT_SHERPA_RUNTIME_URL:-}"
if [[ ! "${expected_sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
  echo "COCKPIT_SHERPA_RUNTIME_SHA256 must be the reviewed runtime archive SHA-256" >&2
  exit 2
fi

downloads_dir="${ai_root}/downloads"
mkdir -p "${downloads_dir}" "${runtime_root}"
if [[ -z "${archive}" && -n "${url}" ]]; then
  archive="${downloads_dir}/sherpa-onnx-${version}-runtime.tar.gz"
  if [[ ! -f "${archive}" ]] ||
     ! printf '%s  %s\n' "${expected_sha256}" "${archive}" | sha256sum --check --status; then
    curl -fL --retry 3 --connect-timeout 20 --continue-at - --progress-bar \
      -o "${archive}" "${url}"
  fi
fi
if [[ -z "${archive}" || ! -f "${archive}" ]]; then
  cat >&2 <<EOF
Sherpa-ONNX runtime ${version} is not independently installed.
Provide a reviewed archive containing include/, lib/, and LICENSE:
  COCKPIT_SHERPA_RUNTIME_ARCHIVE=/path/to/runtime.tar.gz
  COCKPIT_SHERPA_RUNTIME_SHA256=<64-hex-sha256>
or additionally set COCKPIT_SHERPA_RUNTIME_URL for resumable download.
EOF
  exit 2
fi
printf '%s  %s\n' "${expected_sha256}" "${archive}" | sha256sum --check --strict

tmp_dir="$(mktemp -d "${runtime_root}/.${version}.prepare.XXXXXX")"
candidate_dir="${runtime_root}/.${version}.candidate.$$"
backup_dir="${runtime_root}/.${version}.previous.$$"
cleanup() {
  rm -rf --one-file-system "${tmp_dir}" "${candidate_dir}"
  if [[ -d "${backup_dir}" && ! -e "${runtime_dir}" ]]; then
    mv "${backup_dir}" "${runtime_dir}"
  fi
}
trap cleanup EXIT

case "${archive}" in
  *.tar|*.tar.gz|*.tgz|*.tar.xz|*.tar.bz2)
    tar -xf "${archive}" -C "${tmp_dir}"
    ;;
  *.zip)
    unzip -q "${archive}" -d "${tmp_dir}"
    ;;
  *)
    echo "unsupported Sherpa runtime archive: ${archive}" >&2
    exit 2
    ;;
esac

include_dir="$(find "${tmp_dir}" -mindepth 1 -maxdepth 4 -type d -name include -print -quit)"
[[ -n "${include_dir}" ]] || {
  echo "Sherpa runtime archive does not contain include/" >&2
  exit 1
}
archive_root="$(dirname "${include_dir}")"
[[ -d "${archive_root}/lib" ]] || {
  echo "Sherpa runtime archive does not contain lib/ next to include/" >&2
  exit 1
}
license_file="$(find "${archive_root}" -maxdepth 3 -type f \( -iname 'LICENSE' -o -iname 'LICENSE.txt' \) -print -quit)"
[[ -n "${license_file}" ]] || {
  echo "Sherpa runtime archive does not contain a LICENSE file" >&2
  exit 1
}

mkdir "${candidate_dir}"
cp -a "${archive_root}/include" "${archive_root}/lib" "${candidate_dir}/"
cp "${license_file}" "${candidate_dir}/LICENSE"
license_sha256="$(sha256sum "${candidate_dir}/LICENSE" | awk '{print $1}')"
{
  printf 'version=%s\n' "${version}"
  printf 'archive_sha256=%s\n' "${expected_sha256,,}"
  printf 'license_sha256=%s\n' "${license_sha256}"
  printf 'source_archive=%s\n' "$(basename "${archive}")"
} >"${candidate_dir}/MANIFEST.txt"
validate_runtime "${candidate_dir}"

if [[ -e "${runtime_dir}" ]]; then
  mv "${runtime_dir}" "${backup_dir}"
fi
mv "${candidate_dir}" "${runtime_dir}"
temporary_link="${runtime_root}/.current.new.$$"
ln -s "${version}" "${temporary_link}"
mv -Tf "${temporary_link}" "${runtime_root}/current"
rm -rf --one-file-system "${backup_dir}"
trap - EXIT
rm -rf --one-file-system "${tmp_dir}"
printf 'Sherpa-ONNX runtime prepared and verified: %s\n' "${runtime_dir}"
