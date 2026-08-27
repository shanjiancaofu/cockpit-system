#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
if ! command -v rg >/dev/null 2>&1; then
  echo "ripgrep is required for repository dependency checks" >&2
  exit 1
fi


cockpit_native_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "x86_64" ;;
    aarch64|arm64) echo "arm64" ;;
    *) echo "unsupported native architecture: $(uname -m)" >&2; return 1 ;;
  esac
}

cockpit_output_dir() { echo "${COCKPIT_OUTPUT_DIR:-${root_dir}/_output}"; }
cockpit_default_debug_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-debug"; }
cockpit_default_release_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-release"; }
cockpit_default_runtime_dir() { echo "$(cockpit_output_dir)/runtime"; }


build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
parallel_level="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
repo_dir="${root_dir}"
source_filter="^${repo_dir}/(cockpit|tests|tools)/.*"
header_filter="^${repo_dir}/(cockpit|tests|tools)/.*"
driver_source_dir="${COCKPIT_DRIVER_SOURCE_DIR:-${root_dir}/cockpit/drivers}"

driver_dependency_errors=""
if driver_dependency_errors="$(
  rg -n '#include[[:space:]]+"(cockpit/(core|modules|library|navigator)/|agent/)' \
    "${driver_source_dir}" --glob '*.{cc,h}'
)"; then
  :
else
  scan_status=$?
  if [[ "${scan_status}" -ne 1 ]]; then
    echo "driver dependency boundary scan failed" >&2
    exit "${scan_status}"
  fi
fi
if [[ -n "${driver_dependency_errors}" ]]; then
  echo "driver dependency boundary violated:" >&2
  echo "${driver_dependency_errors}" >&2
  exit 1
fi

driver_cmake_errors=""
if driver_cmake_errors="$(
  rg -n '^[[:space:]]*(audio_|camera_|can|vehicle|config|logging|runtime|contracts|agent_)($|[[:space:]])' \
    "${driver_source_dir}" --glob 'CMakeLists.txt'
)"; then
  :
else
  scan_status=$?
  if [[ "${scan_status}" -ne 1 ]]; then
    echo "driver CMake dependency boundary scan failed" >&2
    exit "${scan_status}"
  fi
fi
if [[ -n "${driver_cmake_errors}" ]]; then
  echo "driver CMake links a cockpit project target:" >&2
  echo "${driver_cmake_errors}" >&2
  exit 1
fi

if [[ "${1:-}" == "--driver-boundaries-only" ]]; then
  echo "driver dependency boundaries passed"
  exit 0
fi

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
