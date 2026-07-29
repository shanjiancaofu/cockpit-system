#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/common.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
parallel_level="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
repo_dir="${root_dir}"
source_filter="^${repo_dir}/(cockpit|tests|tools)/.*"
header_filter="^${repo_dir}/(cockpit|tests|tools)/.*"

if [[ ! "${parallel_level}" =~ ^[1-9][0-9]*$ ]]; then
  echo "CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer; found '${parallel_level}'" >&2
  exit 2
fi

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
  bash "${root_dir}/scripts/build.sh" --type debug
fi

package_info="${build_dir}/package-info.env"
if [[ ! -f "${package_info}" ]]; then
  echo "build metadata not found: ${package_info}" >&2
  echo "Reconfigure it with: bash ${root_dir}/scripts/build.sh --type debug" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${package_info}"
if [[ "${COCKPIT_COMPILER_ID}" != "GNU" ]]; then
  echo "clang-tidy requires the GCC Debug compile database; found '${COCKPIT_COMPILER_ID:-unknown}' in ${build_dir}" >&2
  echo "Reconfigure it with: bash ${root_dir}/scripts/build.sh --type debug" >&2
  exit 1
fi

cmake --build "${build_dir}"

"${run_tidy_command}" -quiet -p "${build_dir}" -j "${parallel_level}" \
  -header-filter "${header_filter}" "${source_filter}"
