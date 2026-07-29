#!/usr/bin/env bash
set -euo pipefail

package_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
install_root="${COCKPIT_ROOT:-/cockpit-system}"
install_systemd="${INSTALL_SYSTEMD:-true}"
service_user="cockpit"
service_group="cockpit"
trusted_public_key="${install_root}/config/ota-public-key.pem"
if [[ -f "${trusted_public_key}" ]]; then
  public_key="${trusted_public_key}"
else
  public_key="${COCKPIT_OTA_PUBLIC_KEY:-}"
fi
if [[ ! -f "${public_key}" ]]; then
  echo "trusted OTA public key is missing" >&2
  echo "first install: sudo COCKPIT_OTA_PUBLIC_KEY=/path/to/ota-public-key.pem bash deploy/install.sh" >&2
  exit 1
fi
if [[ ! -f "${package_root}/manifest/SHA256SUMS.sig" ]]; then
  echo "package manifest signature is missing" >&2
  exit 1
fi
if ! openssl pkeyutl -verify -pubin -inkey "${public_key}" -rawin \
     -in "${package_root}/manifest/SHA256SUMS" \
     -sigfile "${package_root}/manifest/SHA256SUMS.sig" >/dev/null 2>&1; then
  echo "package manifest signature verification failed" >&2
  exit 1
fi
if [[ ! -f "${package_root}/manifest/SHA256SUMS" ]] ||
   ! (cd "${package_root}" && sha256sum --check --quiet manifest/SHA256SUMS); then
  echo "package checksum verification failed" >&2
  exit 1
fi
version="$(<"${package_root}/manifest/VERSION")"
if [[ ! "${version}" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
  echo "invalid package version: ${version}" >&2
  exit 1
fi
release_dir="${install_root}/releases/${version}"

if [[ "${install_systemd}" == "true" && "${EUID}" -ne 0 ]]; then
  echo "systemd installation requires root; run with sudo or set INSTALL_SYSTEMD=false" >&2
  exit 1
fi
if [[ "${install_systemd}" == "true" && "${install_root}" != "/cockpit-system" ]]; then
  echo "systemd installation requires COCKPIT_ROOT=/cockpit-system" >&2
  exit 1
fi

if [[ "${install_systemd}" == "true" ]]; then
  if ! getent group "${service_group}" >/dev/null; then
    groupadd --system "${service_group}"
  fi
  if ! id -u "${service_user}" >/dev/null 2>&1; then
    useradd --system --gid "${service_group}" --home-dir /nonexistent \
      --shell /usr/sbin/nologin "${service_user}"
  fi
  for device_group in audio video render dialout; do
    if getent group "${device_group}" >/dev/null; then
      usermod --append --groups "${device_group}" "${service_user}"
    fi
  done
fi

install -d "${install_root}" "${install_root}/releases" "${release_dir}" \
  "${install_root}/config" "${install_root}/models/sensevoice" "${install_root}/data" \
  "${install_root}/logs" "${install_root}/run"
install -d -m 0700 "${install_root}/data/ota/incoming"
cp -a "${package_root}/release/." "${release_dir}/"
install -d "${release_dir}/manifest"
cp -a "${package_root}/manifest/." "${release_dir}/manifest/"

if [[ ! -f "${install_root}/config/config.yaml" ]]; then
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml"
else
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml.new"
  echo "kept config.yaml; new template written to config.yaml.new"
fi
if [[ ! -f "${trusted_public_key}" ]]; then
  install -m 0444 "${public_key}" "${trusted_public_key}"
fi
if [[ "${install_systemd}" == "true" ]]; then
  chown "${service_user}:${service_group}" "${install_root}" "${install_root}/releases"
  chown -R "${service_user}:${service_group}" "${release_dir}" "${install_root}/data" \
    "${install_root}/logs" "${install_root}/run"
  chown -R root:"${service_group}" "${install_root}/config" "${install_root}/models"
  chmod 0750 "${install_root}/config" "${install_root}/models" \
    "${install_root}/models/sensevoice"
fi
# Make the candidate contents durable before publishing the new current link.
sync -f "${install_root}"
ln -sfn "releases/${version}" "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
sync -f "${install_root}"

if [[ "${install_systemd}" == "true" ]]; then
  legacy_units=(
    cockpit.target
    cockpit-development.target
    cockpit-cloud.target
    cockpit-navigator@normal.service
    cockpit-navigator@development.service
    cockpit-navigator@cloud.service
    audio-service.service
    camera-service.service
    cloud-uplink-service.service
    cockpit-gateway-service.service
    cockpit-ui.service
    recording-service.service
    vehicle-data-service.service
    voice-interaction-service.service
  )
  legacy_enabled=false
  legacy_active=false
  for unit in "${legacy_units[@]}"; do
    if systemctl is-enabled --quiet "${unit}" 2>/dev/null; then
      legacy_enabled=true
    fi
    if systemctl is-active --quiet "${unit}" 2>/dev/null; then
      legacy_active=true
    fi
  done
  systemctl disable --now "${legacy_units[@]}" >/dev/null 2>&1 || true
  for unit in "${legacy_units[@]}"; do
    rm -f "/etc/systemd/system/${unit}"
  done
  rm -f /etc/systemd/system/cockpit-navigator@.service
  install -m 0644 "${package_root}/systemd/cockpit-navigator.service" \
    /etc/systemd/system/cockpit-navigator.service
  systemctl daemon-reload
  if [[ "${legacy_enabled}" == "true" ]]; then
    systemctl enable cockpit-navigator.service
  fi
  if [[ "${legacy_active}" == "true" ]]; then
    systemctl restart cockpit-navigator.service
  fi
fi

echo "installed cockpit-system ${version} to ${install_root}"
