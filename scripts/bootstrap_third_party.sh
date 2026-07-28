#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_dir="$(cd -- "${root_dir}/../.." && pwd)"
third_party_dir="${COCKPIT_THIRD_PARTY_DIR:-${workspace_dir}/third_party}"

sherpa_revision="13d0ae6c539d2809d32f5eaa3ef1db0c459d0b24"

checkout_revision() {
  local url="$1"
  local revision="$2"
  local destination="$3"
  if [[ -d "${destination}/.git" ]]; then
    if [[ "$(git -C "${destination}" rev-parse HEAD)" != "${revision}" ]]; then
      echo "existing checkout has a different revision; refusing to overwrite: ${destination}" >&2
      return 1
    fi
    return
  fi

  mkdir -p "${destination}"
  git -C "${destination}" init
  git -C "${destination}" remote add origin "${url}"
  git -C "${destination}" fetch --depth 1 origin "${revision}"
  git -C "${destination}" checkout --detach FETCH_HEAD
  [[ "$(git -C "${destination}" rev-parse HEAD)" == "${revision}" ]]
}

mkdir -p "${third_party_dir}"
sherpa_source="${third_party_dir}/sherpa-onnx"
checkout_revision https://github.com/k2-fsa/sherpa-onnx.git \
  "${sherpa_revision}" "${sherpa_source}"
git -C "${sherpa_source}" submodule update --init --recursive --depth 1

echo "Sherpa-ONNX source is ready in ${sherpa_source}"
