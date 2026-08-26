#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Run Jetson preflight as the development user, not root" >&2
  exit 2
fi

failures=0
check_command() {
  local command_name="$1"
  if command -v "${command_name}" >/dev/null 2>&1; then
    printf 'OK   command %-12s %s\n' "${command_name}" "$(command -v "${command_name}")"
  else
    printf 'MISS command %-12s\n' "${command_name}"
    failures=$((failures + 1))
  fi
}

check_path() {
  local path="$1"
  local description="$2"
  if [[ -e "${path}" ]]; then
    printf 'OK   %-24s %s\n' "${description}" "${path}"
  else
    printf 'MISS %-24s %s\n' "${description}" "${path}"
    failures=$((failures + 1))
  fi
}

echo "Jetson cockpit-system read-only preflight"
printf 'kernel       %s\n' "$(uname -sr)"
printf 'architecture %s\n' "$(uname -m)"
if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "FAIL architecture must be aarch64 on Jetson" >&2
  failures=$((failures + 1))
fi

for command_name in gcc g++ cmake ninja git ros2; do
  check_command "${command_name}"
done
if [[ -f /opt/ros/humble/setup.bash ]]; then
  printf 'OK   ROS2 Humble setup       /opt/ros/humble/setup.bash\n'
else
  printf 'MISS ROS2 Humble setup       /opt/ros/humble/setup.bash\n'
  failures=$((failures + 1))
fi
if command -v nvcc >/dev/null 2>&1; then
  printf 'OK   CUDA compiler            %s\n' "$(command -v nvcc)"
else
  printf 'INFO CUDA compiler            not exposed as nvcc; verify JetPack runtime separately\n'
fi

check_path "/dev/snd" "ALSA device directory"
check_path "/dev/video0" "camera device"
check_path "/sys/class/net/can0" "CAN interface"
check_path "/dev/gpiochip0" "GPIO chip (optional)"

echo
echo "No files, packages, permissions, devices, or services were modified."
if ((failures > 0)); then
  echo "preflight failures=${failures}; resolve them on Jetson before native build" >&2
  exit 1
fi
echo "Jetson preflight passed"
