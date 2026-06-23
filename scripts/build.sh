#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build}"
build_type="${BUILD_TYPE:-Debug}"
generator="${CMAKE_GENERATOR:-Ninja}"

if command -v ninja >/dev/null 2>&1; then
  cmake -S . -B "${build_dir}" -G "${generator}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
  cmake -S . -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
