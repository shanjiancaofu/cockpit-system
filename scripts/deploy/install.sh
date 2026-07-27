#!/usr/bin/env bash
set -euo pipefail

package_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
install_root="${COCKPIT_ROOT:-/cockpit-system}"
install_systemd="${INSTALL_SYSTEMD:-true}"
public_key="${COCKPIT_OTA_PUBLIC_KEY:-${install_root}/config/ota-public-key.pem}"
if [[ ! -f "${public_key}" ]]; then
  echo "trusted OTA public key is missing: ${public_key}" >&2
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

install -d "${release_dir}" "${install_root}/config" "${install_root}/models/whisper" \
  "${install_root}/models/sensevoice" "${install_root}/data" "${install_root}/logs" \
  "${install_root}/run"
install -d -m 0700 "${install_root}/data/ota/incoming"
cp -a "${package_root}/release/." "${release_dir}/"

if [[ ! -f "${install_root}/config/config.yaml" ]]; then
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml"
else
  install -m 0644 "${package_root}/config/config.example.yaml" \
    "${install_root}/config/config.yaml.new"
  echo "kept config.yaml; new template written to config.yaml.new"
fi
if [[ ! -f "${install_root}/config/environment" ]]; then
  install -m 0644 "${package_root}/config/environment.example" \
    "${install_root}/config/environment"
else
  install -m 0644 "${package_root}/config/environment.example" \
    "${install_root}/config/environment.new"
  echo "kept environment; new template written to environment.new"
fi

# Make the candidate contents durable before publishing the new current link.
sync -f "${install_root}"
ln -sfn "releases/${version}" "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
sync -f "${install_root}"

if [[ "${install_systemd}" == "true" ]]; then
  legacy_units=(
    audio-service.service
    camera-service.service
    cloud-uplink-service.service
    cockpit-gateway-service.service
    cockpit-ui.service
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
