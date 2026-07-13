#!/usr/bin/env bash
set -euo pipefail

package_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
install_root="${COCKPIT_ROOT:-/cockpit-system}"
install_systemd="${INSTALL_SYSTEMD:-true}"
version="$(<"${package_root}/manifest/VERSION")"
release_dir="${install_root}/releases/${version}"

if [[ "${install_systemd}" == "true" && "${EUID}" -ne 0 ]]; then
  echo "systemd installation requires root; run with sudo or set INSTALL_SYSTEMD=false" >&2
  exit 1
fi

install -d "${release_dir}" "${install_root}/config" "${install_root}/models/whisper" \
  "${install_root}/data" "${install_root}/logs" "${install_root}/run"
cp -a "${package_root}/release/." "${release_dir}/"

if [[ ! -f "${install_root}/config/config.yaml" ]]; then
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml"
else
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml.new"
  echo "kept config.yaml; new template written to config.yaml.new"
fi

ln -sfn "releases/${version}" "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"

if [[ "${install_systemd}" == "true" ]]; then
  legacy_units=(
    audio-service.service
    camera-service.service
    cloud-uplink-service.service
    cockpit-gateway-service.service
    recording-service.service
    vehicle-data-service.service
    voice-interaction-service.service
  )
  systemctl disable --now "${legacy_units[@]}" >/dev/null 2>&1 || true
  for unit in "${legacy_units[@]}"; do
    rm -f "/etc/systemd/system/${unit}"
  done
  install -m 0644 "${package_root}"/systemd/*.service /etc/systemd/system/
  install -m 0644 "${package_root}"/systemd/*.target /etc/systemd/system/
  systemctl daemon-reload
fi

echo "installed cockpit-system ${version} to ${install_root}"
