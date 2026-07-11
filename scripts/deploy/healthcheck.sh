#!/usr/bin/env bash
set -euo pipefail

install_root="${COCKPIT_ROOT:-/cockpit-system}"
exec "${install_root}/current/bin/cockpit-ctl" status \
  --config "${install_root}/config/config.yaml"
