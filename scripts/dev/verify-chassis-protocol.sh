#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
pin_file="${root_dir}/configs/chassis-protocol-pins.env"

# shellcheck disable=SC1090
source "${pin_file}"

protocol_root="${COCKPIT_CHASSIS_PROTOCOL_ROOT:-${root_dir}/../${COCKPIT_CHASSIS_PROTOCOL_REPOSITORY}}"
protocol_file="${protocol_root}/${COCKPIT_CHASSIS_PROTOCOL_FILE}"

if [[ ! -d "${protocol_root}/.git" ]]; then
  echo "chassis protocol repository not found: ${protocol_root}" >&2
  exit 2
fi
if [[ ! -f "${protocol_file}" ]]; then
  echo "chassis protocol file not found: ${protocol_file}" >&2
  exit 2
fi

actual_revision="$(git -C "${protocol_root}" rev-parse HEAD)"
if [[ "${actual_revision}" != "${COCKPIT_CHASSIS_PROTOCOL_REVISION}" ]]; then
  echo "chassis protocol revision mismatch: expected ${COCKPIT_CHASSIS_PROTOCOL_REVISION}, got ${actual_revision}" >&2
  exit 1
fi

printf '%s  %s\n' "${COCKPIT_CHASSIS_PROTOCOL_SHA256}" "${protocol_file}" | sha256sum --check --status || {
  echo "chassis protocol SHA-256 mismatch: ${protocol_file}" >&2
  exit 1
}

echo "chassis protocol verified revision=${actual_revision} sha256=${COCKPIT_CHASSIS_PROTOCOL_SHA256}"
