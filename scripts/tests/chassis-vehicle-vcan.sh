#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 VEHICLE_CHASSIS_VCAN_TEST" >&2
  exit 2
fi

test_binary="$(realpath "$1")"

if [[ "${CHASSIS_VCAN_NAMESPACE:-}" != 1 ]]; then
  if ! unshare -Urn true >/dev/null 2>&1; then
    echo "SKIP: runner does not permit an unprivileged user/network namespace" >&2
    exit 77
  fi
  exec unshare -Urn env CHASSIS_VCAN_NAMESPACE=1 "$0" "${test_binary}"
fi

if ! command -v ip >/dev/null 2>&1; then
  echo "SKIP: iproute2 is unavailable" >&2
  exit 77
fi
if ! ip link add dev vcan0 type vcan >/dev/null 2>&1; then
  echo "SKIP: runner kernel does not provide vcan in the isolated namespace" >&2
  exit 77
fi
if ! ip link set dev vcan0 up >/dev/null 2>&1; then
  echo "SKIP: runner cannot activate the isolated vcan interface" >&2
  exit 77
fi
exec "${test_binary}" vcan0
