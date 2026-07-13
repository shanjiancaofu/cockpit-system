#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
iterations="${1:-20}"

if ! [[ "${iterations}" =~ ^[1-9][0-9]*$ ]]; then
  echo "iterations must be a positive integer" >&2
  exit 2
fi

for ((iteration = 1; iteration <= iterations; ++iteration)); do
  if ! output="$("${build_dir}/bin/camera_synthetic_stability_test" 2>&1)"; then
    printf '%s\n' "${output}" >&2
    printf 'camera synthetic stability iteration %d/%d failed\n' "${iteration}" "${iterations}" >&2
    exit 1
  fi
  printf 'camera synthetic stability iteration %d/%d passed\n' "${iteration}" "${iterations}"
done
