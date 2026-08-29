#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Jetson camera calibration gate requires an ARM64 target host" >&2
  exit 2
fi
if [[ ! -e /dev/video0 ]]; then
  echo "IMX219 V4L2 device /dev/video0 is not present" >&2
  exit 2
fi

build_dir="${BUILD_DIR:-$(cockpit_default_debug_build_dir)}"
output_dir="${CALIBRATION_OUTPUT_DIR:-${root_dir}/_output/runtime/camera-calibration/imx219-q12-70-5}"
frames="${CALIBRATION_FRAMES:-30}"
timeout_seconds="${CALIBRATION_TIMEOUT_SECONDS:-300}"

BUILD_DIR="${build_dir}" "${root_dir}/scripts/build.sh" --arch arm64 --type debug --no-test
"${root_dir}/scripts/tests/ctest.sh" --test-dir "${build_dir}" --output-on-failure \
  -R '^camera_calibrator_(cli|offline)_test$'
"${build_dir}/bin/camera-calibrator" \
  --device nvargus://0 \
  --width 1920 \
  --height 1080 \
  --fps 30 \
  --frames "${frames}" \
  --timeout-seconds "${timeout_seconds}" \
  --board-profile q12-70-5 \
  --output-dir "${output_dir}"
