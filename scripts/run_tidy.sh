#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
jobs="${JOBS:-$(nproc)}"
repo_dir="$(pwd -P)"
header_filter="^${repo_dir}/(apps|core|drivers|modules|services|tests|tools)/.*"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not found. Install it with: sudo apt-get install -y clang-tidy" >&2
  exit 1
fi

if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
  echo "${build_dir}/compile_commands.json not found; configuring build first."
  bash scripts/build.sh
fi

mapfile -t sources < <(
  git ls-files '*.cc' '*.cpp' '*.cxx' '*.c' |
    grep -v -E '(^build/|\.pb\.cc$|\.grpc\.pb\.cc$)'
)

if [[ "${#sources[@]}" -eq 0 ]]; then
  echo "no C/C++ sources found"
  exit 0
fi

printf '%s\n' "${sources[@]}" |
  xargs -r -n 1 -P "${jobs}" clang-tidy -p "${build_dir}" --header-filter="${header_filter}"
