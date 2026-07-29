#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/common.sh"

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
export COCKPIT_RUNTIME_DIR="${COCKPIT_RUNTIME_DIR:-$(cockpit_default_runtime_dir)}"
bin_dir="${BIN_DIR:-${build_dir}/bin}"
config_path="${CONFIG_PATH:-${root_dir}/configs/development.yaml}"

exec "${bin_dir}/cockpit-ctl" health --config "${config_path}"
