#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
jobs="${JOBS:-$(nproc)}"
repo_dir="$(pwd -P)"
source_filter="^${repo_dir}/(cockpit|tests|tools)/.*"
header_filter="^${repo_dir}/(cockpit|tests|tools)/.*"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not found. Install it with: sudo apt-get install -y clang-tidy" >&2
  exit 1
fi

run_tidy_command=""
if command -v run-clang-tidy >/dev/null 2>&1; then
  run_tidy_command="run-clang-tidy"
elif command -v run-clang-tidy-14 >/dev/null 2>&1; then
  run_tidy_command="run-clang-tidy-14"
else
  echo "run-clang-tidy not found. Install it with: sudo apt-get install -y clang-tidy" >&2
  exit 1
fi

if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "${build_dir}/compile_commands.json not found; configuring build first."
  bash scripts/build.sh
fi

cmake --build "${build_dir}" --parallel "${jobs}"

"${run_tidy_command}" -quiet -p "${build_dir}" -j "${jobs}" \
  -header-filter "${header_filter}" "${source_filter}"
