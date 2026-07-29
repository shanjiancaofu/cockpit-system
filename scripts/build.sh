#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/common.sh"

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [options] [-- CMake options]

Options:
  --arch x86_64|arm64   Target architecture (default: current machine)
  --type debug|release  Build type (default: debug)
  --no-test             Do not run tests after building
  -h, --help            Show this help

Environment:
  COCKPIT_OUTPUT_DIR    Override the WSL output root (default: _output)
  BUILD_DIR             Override _output/build/<arch>-<type>
  JETSON_SYSROOT        Jetson root filesystem used for x86_64 -> arm64 builds
  TOOLCHAIN_FILE        Override the ARM64 CMake toolchain file
EOF
}

normalize_arch() {
  case "$1" in
    x86_64|amd64) echo "x86_64" ;;
    arm64|aarch64) echo "arm64" ;;
    *)
      echo "unsupported architecture: $1" >&2
      exit 2
      ;;
  esac
}

target_arch="$(normalize_arch "${TARGET_ARCH:-$(uname -m)}")"
case "${BUILD_TYPE:-Debug}" in
  Debug|debug) build_type="Debug" ;;
  Release|release) build_type="Release" ;;
  *)
    echo "unsupported BUILD_TYPE: ${BUILD_TYPE}" >&2
    exit 2
    ;;
esac
run_tests=true
cmake_options=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch)
      [[ $# -ge 2 ]] || { echo "--arch requires a value" >&2; exit 2; }
      target_arch="$(normalize_arch "$2")"
      shift 2
      ;;
    --type)
      [[ $# -ge 2 ]] || { echo "--type requires a value" >&2; exit 2; }
      case "${2,,}" in
        debug) build_type="Debug" ;;
        release) build_type="Release" ;;
        *)
          echo "unsupported build type: $2" >&2
          exit 2
          ;;
      esac
      shift 2
      ;;
    --no-test)
      run_tests=false
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      cmake_options+=("$@")
      break
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

build_type_name="${build_type,,}"
output_dir="$(cockpit_output_dir)"
build_dir="${BUILD_DIR:-${output_dir}/build/${target_arch}-${build_type_name}}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-${output_dir}/runtime}"
machine_arch="$(normalize_arch "$(uname -m)")"
cross_compiling=false

if ! command -v ninja >/dev/null 2>&1; then
  echo "Ninja is required; install it with: sudo apt install ninja-build" >&2
  exit 1
fi

if [[ -f "${build_dir}/CMakeCache.txt" ]]; then
  configured_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' \
    "${build_dir}/CMakeCache.txt")"
  if [[ "${configured_generator}" != "Ninja" ]]; then
    echo "build directory uses '${configured_generator:-unknown}', but cockpit-system requires Ninja" >&2
    echo "remove the generated build directory and run this command again: ${build_dir}" >&2
    exit 1
  fi
fi

if [[ "${target_arch}" != "${machine_arch}" ]]; then
  if [[ "${machine_arch}" != "x86_64" || "${target_arch}" != "arm64" ]]; then
    echo "unsupported cross compilation: ${machine_arch} -> ${target_arch}" >&2
    exit 2
  fi

  cross_compiling=true
  run_tests=false
  toolchain_file="${TOOLCHAIN_FILE:-${root_dir}/cmake/toolchains/jetson-aarch64.cmake}"
  [[ -f "${toolchain_file}" ]] || {
    echo "toolchain file not found: ${toolchain_file}" >&2
    exit 1
  }
  [[ -n "${JETSON_SYSROOT:-}" ]] || {
    echo "JETSON_SYSROOT is required for x86_64 -> arm64 builds" >&2
    exit 1
  }
  cmake_options+=(
    "-DCMAKE_TOOLCHAIN_FILE=${toolchain_file}"
    "-DJETSON_SYSROOT=${JETSON_SYSROOT}"
    "-DBUILD_TESTING=OFF"
  )
fi

if [[ "${cross_compiling}" == false ]]; then
  c_compiler="$(command -v gcc || true)"
  cxx_compiler="$(command -v g++ || true)"
  if [[ -z "${c_compiler:-}" || -z "${cxx_compiler:-}" ]]; then
    echo "GCC C/C++ compilers not found" >&2
    exit 1
  fi
  cmake_options+=(
    "-DCMAKE_C_COMPILER=${c_compiler}"
    "-DCMAKE_CXX_COMPILER=${cxx_compiler}"
  )
fi

echo "Configuring ${build_type} with GCC in ${build_dir}"

cmake_args=(
  -S "${root_dir}"
  -B "${build_dir}"
  -G Ninja
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "${cmake_options[@]}"
)
cmake "${cmake_args[@]}"

package_info="${build_dir}/package-info.env"
if [[ ! -f "${package_info}" ]]; then
  echo "CMake did not generate package metadata: ${package_info}" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${package_info}"
configured_build_type="${COCKPIT_BUILD_TYPE}"
configured_compiler_id="${COCKPIT_COMPILER_ID}"
expected_compiler_id="GNU"
if [[ "${configured_build_type}" != "${build_type}" ||
      "${configured_compiler_id}" != "${expected_compiler_id}" ]]; then
  echo "CMake reset cached options while changing compilers; applying the requested configuration again"
  cmake "${cmake_args[@]}"
  # shellcheck disable=SC1090
  source "${package_info}"
  configured_build_type="${COCKPIT_BUILD_TYPE}"
  configured_compiler_id="${COCKPIT_COMPILER_ID}"
fi
if [[ "${configured_build_type}" != "${build_type}" ||
      "${configured_compiler_id}" != "${expected_compiler_id}" ]]; then
  echo "configured toolchain does not match the request: type=${configured_build_type:-unknown}, compiler=${configured_compiler_id:-unknown}" >&2
  exit 1
fi

cmake --build "${build_dir}"

if [[ "${run_tests}" == true ]]; then
  ctest --test-dir "${build_dir}" --output-on-failure
elif [[ "${cross_compiling}" == true ]]; then
  echo "tests skipped: ARM64 binaries cannot run on the x86_64 build machine"
else
  echo "tests skipped"
fi
