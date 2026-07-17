#!/usr/bin/env bash
set -euo pipefail

cockpit_ctl="$1"
config="$2"
work_dir="$(mktemp -d /tmp/cockpit-diagnostic-snapshot-test-XXXXXX)"
trap 'rm -rf "${work_dir}"' EXIT

runtime_dir="${work_dir}/runtime"
snapshot_dir="${work_dir}/snapshot"
mkdir -p "${runtime_dir}/logs"
for index in $(seq -w 0 31); do
  printf 'old log' >"${runtime_dir}/logs/a${index}.log"
  touch -d '2000-01-01 UTC' "${runtime_dir}/logs/a${index}.log"
done
printf 'abcdefghijklmnopqrstuvwxyz' >"${runtime_dir}/logs/navigator.log"
printf 'not a log' >"${runtime_dir}/logs/ignored.txt"

COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
  --directory "${snapshot_dir}" --socket "${work_dir}/missing.sock" --max-log-bytes 8

[[ -f "${snapshot_dir}/manifest.json" ]]
[[ -f "${snapshot_dir}/service_status.json" ]]
[[ -f "${snapshot_dir}/runtime_status.txt" ]]
[[ "$(<"${snapshot_dir}/logs/navigator.log")" == "stuvwxyz" ]]
[[ ! -e "${snapshot_dir}/logs/ignored.txt" ]]
[[ ! -e "${snapshot_dir}/config.yaml" ]]
grep -q '"runtime":{"available":false' "${snapshot_dir}/manifest.json"
grep -q '"retention":{"max_snapshots":10,"max_total_bytes":104857600}' \
  "${snapshot_dir}/manifest.json"
grep -q '"logs_omitted":1,"logs_failed":0' "${snapshot_dir}/manifest.json"
grep -q '"name":"navigator.log","bytes":8,"truncated":true' \
  "${snapshot_dir}/manifest.json"
grep -q '"services"' "${snapshot_dir}/service_status.json"

set +e
COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
  --directory "${snapshot_dir}" --socket "${work_dir}/missing.sock" >/dev/null 2>&1
existing_result=$?
COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
  --directory "${work_dir}/invalid" --max-log-bytes 0 >/dev/null 2>&1
invalid_result=$?
set -e

[[ "${existing_result}" -eq 1 ]]
[[ "${invalid_result}" -eq 2 ]]

retention_dir="${work_dir}/retention"
mkdir -p "${retention_dir}"
COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
  --directory "${retention_dir}/snapshot-1" --socket "${work_dir}/missing.sock" \
  --max-snapshots 10 --max-total-bytes 1048576 >/dev/null
dd if=/dev/zero of="${retention_dir}/snapshot-1/large.bin" bs=1024 count=64 status=none
COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
  --directory "${retention_dir}/snapshot-2" --socket "${work_dir}/missing.sock" \
  --max-snapshots 10 --max-total-bytes 32768 >/dev/null
[[ ! -e "${retention_dir}/snapshot-1" ]]
[[ -d "${retention_dir}/snapshot-2" ]]

for index in 3 4; do
  COCKPIT_RUNTIME_DIR="${runtime_dir}" "${cockpit_ctl}" snapshot --config "${config}" \
    --directory "${retention_dir}/snapshot-${index}" --socket "${work_dir}/missing.sock" \
    --max-snapshots 2 --max-total-bytes 1048576 >/dev/null
done
[[ ! -e "${retention_dir}/snapshot-2" ]]
[[ -d "${retention_dir}/snapshot-3" ]]
[[ -d "${retention_dir}/snapshot-4" ]]
