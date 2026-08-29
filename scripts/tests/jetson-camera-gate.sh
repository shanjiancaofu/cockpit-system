#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Jetson camera gate requires an ARM64 target host" >&2
  exit 2
fi

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
device="${JETSON_CAMERA_DEVICE:-/dev/video0}"
width="${JETSON_CAMERA_WIDTH:-1920}"
height="${JETSON_CAMERA_HEIGHT:-1080}"
fps="${JETSON_CAMERA_FPS:-30}"
frames="${JETSON_CAMERA_FRAMES:-300}"
timeout_ms="${JETSON_CAMERA_TIMEOUT_MS:-20000}"

BUILD_DIR="${build_dir}" "${root_dir}/scripts/build.sh" \
  --arch arm64 --type debug --no-test
"${root_dir}/scripts/tests/ctest.sh" \
  --test-dir "${build_dir}" --output-on-failure -R '^raw10_unpack_test$'
"${build_dir}/bin/camera-preview-probe" \
  --backend software_isp \
  --device "${device}" \
  --width "${width}" \
  --height "${height}" \
  --fps "${fps}" \
  --frames "${frames}" \
  --timeout-ms "${timeout_ms}" \
  --config "${root_dir}/configs/development.yaml"
