#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [options] [-- CMake options]

Options:
  --arch x86_64|arm64   Target architecture (default: current machine)
  --type debug|release  Build type (default: debug)
  --compiler clang|gcc  Compiler (default: clang for Debug, gcc for Release)
  --no-test             Do not run tests after building
  -h, --help            Show this help

Environment:
  COCKPIT_OUTPUT_DIR    Override the WSL output root (default: _output)
  BUILD_DIR             Override _output/build/<arch>-<type>
  COMPILER              Override the default compiler (clang or gcc)
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
compiler_family="${COMPILER:-}"
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
    --compiler)
      [[ $# -ge 2 ]] || { echo "--compiler requires a value" >&2; exit 2; }
      compiler_family="${2,,}"
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

compiler_family="${compiler_family,,}"
if [[ -z "${compiler_family}" ]]; then
  if [[ "${build_type}" == "Debug" ]]; then
    compiler_family="clang"
  else
    compiler_family="gcc"
  fi
fi
if [[ "${compiler_family}" != "clang" && "${compiler_family}" != "gcc" ]]; then
  echo "unsupported compiler: ${compiler_family}" >&2
  exit 2
fi

build_type_name="${build_type,,}"
output_dir="$(cockpit_output_dir)"
build_dir="${BUILD_DIR:-${output_dir}/build/${target_arch}-${build_type_name}}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-${output_dir}/runtime}"
generator="${CMAKE_GENERATOR:-Ninja}"
machine_arch="$(normalize_arch "$(uname -m)")"
cross_compiling=false

if [[ "${target_arch}" != "${machine_arch}" ]]; then
  if [[ "${machine_arch}" != "x86_64" || "${target_arch}" != "arm64" ]]; then
    echo "unsupported cross compilation: ${machine_arch} -> ${target_arch}" >&2
    exit 2
  fi

  cross_compiling=true
  run_tests=false
  if [[ "${compiler_family}" != "gcc" ]]; then
    echo "cross compilation currently requires the GCC Jetson toolchain" >&2
    exit 2
  fi
  toolchain_file="${TOOLCHAIN_FILE:-cmake/toolchains/jetson-aarch64.cmake}"
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
    "-DBUILD_TESTS=OFF"
  )
fi

if [[ "${cross_compiling}" == false ]]; then
  if [[ "${compiler_family}" == "clang" ]]; then
    for candidate in clang++ clang++-18 clang++-17 clang++-16 clang++-15 clang++-14; do
      if command -v "${candidate}" >/dev/null 2>&1; then
        cxx_compiler="$(command -v "${candidate}")"
        break
      fi
    done
  else
    cxx_compiler="$(command -v g++ || true)"
  fi
  if [[ -z "${cxx_compiler:-}" ]]; then
    echo "${compiler_family} C++ compiler not found" >&2
    exit 1
  fi
  cmake_options+=("-DCMAKE_CXX_COMPILER=${cxx_compiler}")
fi

echo "Configuring ${build_type} with ${compiler_family} in ${build_dir}"

cmake_args=(
  -S .
  -B "${build_dir}"
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "${cmake_options[@]}"
)
if command -v ninja >/dev/null 2>&1; then
  cmake_args+=(-G "${generator}")
fi
cmake "${cmake_args[@]}"

configured_build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build_dir}/CMakeCache.txt")"
configured_compiler_id="$(sed -n 's/^COCKPIT_COMPILER_ID:STRING=//p' "${build_dir}/CMakeCache.txt")"
expected_compiler_id="GNU"
if [[ "${compiler_family}" == "clang" ]]; then
  expected_compiler_id="Clang"
fi
if [[ "${configured_build_type}" != "${build_type}" ||
      "${configured_compiler_id}" != "${expected_compiler_id}" ]]; then
  echo "CMake reset cached options while changing compilers; applying the requested configuration again"
  cmake "${cmake_args[@]}"
  configured_build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build_dir}/CMakeCache.txt")"
  configured_compiler_id="$(sed -n 's/^COCKPIT_COMPILER_ID:STRING=//p' "${build_dir}/CMakeCache.txt")"
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
