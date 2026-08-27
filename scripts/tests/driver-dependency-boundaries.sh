#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
lint_script="${root_dir}/scripts/dev/lint.sh"
test_root="$(mktemp -d /tmp/cockpit-driver-boundary-test.XXXXXX)"
trap 'rm -rf --one-file-system "${test_root}"' EXIT

clean_driver_dir="${test_root}/clean/drivers"
include_violation_dir="${test_root}/include-violation/drivers"
cmake_violation_dir="${test_root}/cmake-violation/drivers"
missing_tool_bin="${test_root}/missing-tool-bin"
mkdir -p "${clean_driver_dir}" "${include_violation_dir}" "${cmake_violation_dir}" \
  "${missing_tool_bin}"

printf '%s\n' 'add_library(clean_driver INTERFACE)' >"${clean_driver_dir}/CMakeLists.txt"
printf '%s\n' '#pragma once' >"${clean_driver_dir}/clean_driver.h"
COCKPIT_DRIVER_SOURCE_DIR="${clean_driver_dir}" \
  bash "${lint_script}" --driver-boundaries-only >/dev/null

printf '%s\n' '#include "cockpit/modules/audio/frames/audio_frame.h"' \
  >"${include_violation_dir}/bad_driver.cc"
printf '%s\n' 'add_library(bad_driver INTERFACE)' \
  >"${include_violation_dir}/CMakeLists.txt"
if COCKPIT_DRIVER_SOURCE_DIR="${include_violation_dir}" \
  bash "${lint_script}" --driver-boundaries-only >/dev/null 2>&1; then
  echo "driver upward include was not rejected" >&2
  exit 1
fi

printf '%s\n' \
  'target_link_libraries(bad_driver' \
  '    PRIVATE' \
  '        logging' \
  ')' >"${cmake_violation_dir}/CMakeLists.txt"
if COCKPIT_DRIVER_SOURCE_DIR="${cmake_violation_dir}" \
  bash "${lint_script}" --driver-boundaries-only >/dev/null 2>&1; then
  echo "driver project CMake dependency was not rejected" >&2
  exit 1
fi

ln -s "$(command -v dirname)" "${missing_tool_bin}/dirname"
missing_tool_output=""
if missing_tool_output="$(
  PATH="${missing_tool_bin}" /bin/bash "${lint_script}" --driver-boundaries-only 2>&1
)"; then
  echo "driver boundary check passed without ripgrep" >&2
  exit 1
fi
if [[ "${missing_tool_output}" != *"ripgrep is required"* ]]; then
  echo "missing ripgrep did not produce an actionable error" >&2
  exit 1
fi

echo "driver dependency boundary tests passed"
