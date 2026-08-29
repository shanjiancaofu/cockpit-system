#!/usr/bin/env bash

cockpit_native_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "x86_64" ;;
    aarch64|arm64) echo "arm64" ;;
    *)
      echo "unsupported native architecture: $(uname -m)" >&2
      return 1
      ;;
  esac
}

cockpit_repo_root() {
  cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd
}

cockpit_output_dir() {
  echo "${COCKPIT_OUTPUT_DIR:-$(cockpit_repo_root)/_output}"
}

cockpit_default_debug_build_dir() {
  echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-debug"
}

cockpit_default_release_build_dir() {
  echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-release"
}

cockpit_default_runtime_dir() {
  echo "$(cockpit_output_dir)/runtime"
}

cockpit_source_ros2_environment() {
  local required="${1:-optional}"
  if [[ -n "${AMENT_PREFIX_PATH:-}" ]]; then
    return 0
  fi
  local setup_file="${ROS2_SETUP_BASH:-/opt/ros/humble/setup.bash}"
  if [[ ! -f "${setup_file}" ]]; then
    if [[ "${required}" == "required" ]]; then
      echo "ROS 2 setup file not found: ${setup_file}" >&2
      return 2
    fi
    return 0
  fi
  set +u
  # shellcheck disable=SC1090
  source "${setup_file}"
  set -u
}
