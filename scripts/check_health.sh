#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lib/build_paths.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
bin_dir="${BIN_DIR:-${build_dir}/bin}"
config_path="${CONFIG_PATH:-configs/config.yaml}"

exec "${bin_dir}/cockpit-ctl" health --config "${config_path}"
