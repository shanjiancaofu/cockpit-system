#!/usr/bin/env bash
set -euo pipefail

interface="${CAN_INTERFACE:-can0}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "CAN interface setup requires root" >&2
  exit 1
fi
if [[ ! -e "/sys/class/net/${interface}" ]]; then
  echo "CAN interface not found: ${interface}" >&2
  exit 1
fi

modprobe can
modprobe can_raw
modprobe mttcan

ip link set "${interface}" down
ip link set "${interface}" type can \
  bitrate 500000 sample-point 0.8 \
  dbitrate 2000000 dsample-point 0.8 \
  fd on one-shot on berr-reporting on restart-ms 1000
ip link set "${interface}" up

ip -details -statistics link show "${interface}"
