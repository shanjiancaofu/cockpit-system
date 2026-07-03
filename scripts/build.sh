#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [options] [-- CMake options]

Options:
  --arch x86_64|arm64   Target architecture (default: current machine)
  --type debug|release  Build type (default: debug)
  --no-test             Do not run tests after building
  -h, --help            Show this help

Environment:
  BUILD_DIR             Override build/<arch>-<type>
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
build_dir="${BUILD_DIR:-build/${target_arch}-${build_type_name}}"
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

if command -v ninja >/dev/null 2>&1; then
  cmake -S . -B "${build_dir}" -G "${generator}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "${cmake_options[@]}"
else
  cmake -S . -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "${cmake_options[@]}"
fi

cmake --build "${build_dir}"

if [[ "${run_tests}" == true ]]; then
  ctest --test-dir "${build_dir}" --output-on-failure
elif [[ "${cross_compiling}" == true ]]; then
  echo "tests skipped: ARM64 binaries cannot run on the x86_64 build machine"
else
  echo "tests skipped"
fi
