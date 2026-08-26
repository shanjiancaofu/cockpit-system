#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ros2-humble-pins.env
source "${SCRIPT_DIR}/ros2-humble-pins.env"

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Do not run this script as root; it uses sudo for individual system operations." >&2
  exit 2
fi

source /etc/os-release
if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "22.04" ||
      "${UBUNTU_CODENAME:-}" != "jammy" ]]; then
  echo "ROS 2 Humble deb setup requires Ubuntu 22.04 Jammy; detected ${PRETTY_NAME:-unknown}" >&2
  exit 2
fi
if [[ "$(dpkg --print-architecture)" != "amd64" ]]; then
  echo "This script targets Ubuntu 22.04 x86_64 (amd64); use a Jetson-specific install flow for ARM64." >&2
  exit 2
fi
if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo is required to install system packages and /opt/ros/humble" >&2
  exit 2
fi

sudo -v

sudo apt-get update
sudo apt-get install -y --allow-downgrades \
  ca-certificates \
  curl \
  gnupg2 \
  lsb-release \
  software-properties-common
sudo add-apt-repository -y universe

sudo curl -fsSL \
  https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu jammy main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null

sudo apt-get update
sudo apt-get install -y \
  "ros-humble-ros-base=${ROS_BASE_VERSION}" \
  "ros-humble-rclcpp=${RCLCPP_VERSION}" \
  "ros-humble-rclcpp-action=${RCLCPP_ACTION_VERSION}" \
  "ros-humble-nav2-msgs=${NAV2_MSGS_VERSION}" \
  "ros-humble-navigation2=${NAVIGATION2_VERSION}" \
  "ros-humble-nav2-bringup=${NAV2_BRINGUP_VERSION}" \
  "ros-humble-sensor-msgs=${SENSOR_MSGS_VERSION}" \
  "ros-humble-geometry-msgs=${GEOMETRY_MSGS_VERSION}" \
  "ros-humble-builtin-interfaces=${BUILTIN_INTERFACES_VERSION}" \
  python3-colcon-common-extensions \
  python3-rosdep \
  python3-vcstool

if ! rosdep init >/dev/null 2>&1; then
  if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
    echo "rosdep init failed and no existing default sources were found" >&2
    exit 1
  fi
fi
rosdep update

if ! grep -Fqx 'source /opt/ros/humble/setup.bash' "${HOME}/.bashrc" 2>/dev/null; then
  printf '\nsource /opt/ros/humble/setup.bash\n' >>"${HOME}/.bashrc"
fi

# shellcheck disable=SC1091
source /opt/ros/humble/setup.bash
command -v ros2 >/dev/null
ros2 pkg prefix nav2_bringup >/dev/null
ros2 pkg prefix nav2_msgs >/dev/null
ros2 pkg prefix sensor_msgs >/dev/null
ros2 pkg prefix rclcpp >/dev/null

verify_package() {
  local package="$1"
  local expected="$2"
  local actual
  actual="$(dpkg-query -W -f='${Version}' "${package}")"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "${package} version mismatch: expected ${expected}, got ${actual}" >&2
    exit 1
  fi
}
verify_package ros-humble-ros-base "${ROS_BASE_VERSION}"
verify_package ros-humble-rclcpp "${RCLCPP_VERSION}"
verify_package ros-humble-rclcpp-action "${RCLCPP_ACTION_VERSION}"
verify_package ros-humble-nav2-msgs "${NAV2_MSGS_VERSION}"
verify_package ros-humble-navigation2 "${NAVIGATION2_VERSION}"
verify_package ros-humble-nav2-bringup "${NAV2_BRINGUP_VERSION}"
verify_package ros-humble-sensor-msgs "${SENSOR_MSGS_VERSION}"
verify_package ros-humble-geometry-msgs "${GEOMETRY_MSGS_VERSION}"
verify_package ros-humble-builtin-interfaces "${BUILTIN_INTERFACES_VERSION}"

echo "ROS 2 Humble and Nav2 installation verified"
ros2 --version || true
printf 'ros2=%s\n' "$(ros2 pkg prefix rclcpp)"
printf 'nav2_bringup=%s\n' "$(ros2 pkg prefix nav2_bringup)"
printf 'nav2_msgs=%s\n' "$(ros2 pkg prefix nav2_msgs)"
printf 'sensor_msgs=%s\n' "$(ros2 pkg prefix sensor_msgs)"
