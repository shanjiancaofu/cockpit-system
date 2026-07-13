#!/usr/bin/env bash
set -euo pipefail

safe_ota="$1"
navigator="$2"
module_dir="$3"
source_root="$4"
work_dir="$(mktemp -d /tmp/cockpit-safe-ota-test-XXXXXX)"
navigator_pid=""

cleanup() {
  if [[ -n "${navigator_pid}" ]]; then
    kill "${navigator_pid}" 2>/dev/null || true
    wait "${navigator_pid}" 2>/dev/null || true
  fi
  rm -rf "${work_dir}"
}
trap cleanup EXIT

make_package() {
  local package_root="$1"
  local version="$2"
  mkdir -p "${package_root}/release/bin" "${package_root}/config" \
    "${package_root}/deploy" "${package_root}/manifest"
  printf 'release %s\n' "${version}" >"${package_root}/release/bin/probe"
  printf 'system:\n  name: test\n' >"${package_root}/config/config.example.yaml"
  printf '%s\n' "${version}" >"${package_root}/manifest/VERSION"
  install -m 0755 "${source_root}/scripts/deploy/install.sh" \
    "${package_root}/deploy/install.sh"
  (
    cd "${package_root}"
    find release config deploy manifest -type f ! -name SHA256SUMS -print0 \
      | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
  )
}

install_root="${work_dir}/install"
mkdir -p "${install_root}/releases/1.0.0"
ln -s releases/1.0.0 "${install_root}/current"

package_two="${work_dir}/package-2"
make_package "${package_two}" 2.0.0
set +e
"${safe_ota}" --package "${package_two}" --confirm 2.0.1 --root "${install_root}" \
  --health-command /bin/true --standalone
confirmation_result=$?
set -e
[[ "${confirmation_result}" -eq 2 ]]
[[ "$(readlink "${install_root}/current")" == "releases/1.0.0" ]]

"${safe_ota}" --package "${package_two}" --confirm 2.0.0 --root "${install_root}" \
  --health-command /bin/true --standalone
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]

package_three="${work_dir}/package-3"
make_package "${package_three}" 3.0.0
set +e
"${safe_ota}" --package "${package_three}" --confirm 3.0.0 --root "${install_root}" \
  --health-command /bin/false --timeout 1 --standalone
health_result=$?
set -e
[[ "${health_result}" -eq 3 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/releases/3.0.0" ]]

printf 'tampered\n' >>"${package_three}/release/bin/probe"
set +e
"${safe_ota}" --package "${package_three}" --confirm 3.0.0 --root "${install_root}" \
  --health-command /bin/true --standalone
checksum_result=$?
set -e
[[ "${checksum_result}" -eq 1 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]

package_four="${work_dir}/package-4"
make_package "${package_four}" 4.0.0
install -m 0644 "${source_root}/configs/config.yaml" "${install_root}/config/config.yaml"
socket_path="${install_root}/run/navigator.sock"
"${navigator}" --config "${install_root}/config/config.yaml" --module-dir "${module_dir}" \
  --socket "${socket_path}" >"${work_dir}/navigator.log" 2>&1 &
navigator_pid=$!
for _ in $(seq 1 50); do
  if "${navigator}" --command mode --socket "${socket_path}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
"${navigator}" --command mode --socket "${socket_path}" >/dev/null
"${safe_ota}" --package "${package_four}" --confirm 4.0.0 --root "${install_root}" \
  --socket "${socket_path}" --health-command /bin/true --timeout 10
[[ "$(readlink "${install_root}/current")" == "releases/4.0.0" ]]
[[ "$("${navigator}" --command mode --socket "${socket_path}")" == "OK mode=normal" ]]

package_five="${work_dir}/package-5"
make_package "${package_five}" 5.0.0
set +e
"${safe_ota}" --package "${package_five}" --confirm 5.0.0 --root "${install_root}" \
  --socket "${socket_path}" --health-command /bin/false --timeout 1
runtime_health_result=$?
set -e
[[ "${runtime_health_result}" -eq 3 ]]
[[ "$(readlink "${install_root}/current")" == "releases/4.0.0" ]]
[[ ! -e "${install_root}/releases/5.0.0" ]]
[[ "$("${navigator}" --command mode --socket "${socket_path}")" == "OK mode=normal" ]]
runtime_status="$("${navigator}" --command status --socket "${socket_path}")"
[[ "${runtime_status}" == *"module=transfer state=running"* ]]

"${navigator}" --command shutdown --socket "${socket_path}" >/dev/null
wait "${navigator_pid}"
navigator_pid=""
