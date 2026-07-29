#!/usr/bin/env bash
set -euo pipefail

export CAMERA_REQUIRED=true
export CAMERA_AUTO_START=true
export CAMERA_DEVICE="${CAMERA_DEVICE:-nvargus://0}"

exec "$(dirname -- "${BASH_SOURCE[0]}")/run-cockpit-ui.sh" "$@"
