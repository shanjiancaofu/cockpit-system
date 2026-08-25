#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 VEHICLE_CHASSIS_VCAN_TEST" >&2
  exit 2
fi

test_binary="$(realpath "$1")"

if [[ "${CHASSIS_VCAN_NAMESPACE:-}" != 1 ]]; then
  exec unshare -Urn env CHASSIS_VCAN_NAMESPACE=1 "$0" "${test_binary}"
fi

ip link add dev vcan0 type vcan
ip link set dev vcan0 up
exec "${test_binary}" vcan0
