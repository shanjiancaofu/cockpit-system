#!/usr/bin/env bash
set -euo pipefail

export CAMERA_REQUIRED=true
export CAMERA_AUTO_START=true

exec "$(dirname -- "${BASH_SOURCE[0]}")/run_cockpit_ui.sh" "$@"
