#!/usr/bin/env bash
set -euo pipefail

install_root="${COCKPIT_ROOT:-/cockpit-system}"
version="${1:-}"

if [[ -z "${version}" ]]; then
  echo "usage: rollback.sh VERSION" >&2
  exit 1
fi
if [[ ! "${version}" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
  echo "invalid release version: ${version}" >&2
  exit 1
fi
if [[ ! -d "${install_root}/releases/${version}" ]]; then
  echo "release not found: ${install_root}/releases/${version}" >&2
  exit 1
fi

ln -sfn "releases/${version}" "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
sync -f "${install_root}"
echo "cockpit-system current release is now ${version}"
