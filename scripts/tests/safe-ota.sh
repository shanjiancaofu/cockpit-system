#!/usr/bin/env bash
set -euo pipefail

export QT_QPA_PLATFORM=offscreen

safe_ota="$1"
navigator="$2"
module_dir="$3"
source_root="$4"
work_dir="$(mktemp -d /tmp/cockpit-safe-ota-test-XXXXXX)"
navigator_pid=""
ota_private_key="${work_dir}/ota-private.pem"
ota_public_key="${work_dir}/ota-public.pem"

cleanup() {
  local status=$?
  if [[ "${status}" -ne 0 ]]; then
    echo "safe-ota failure diagnostics:" >&2
    echo "navigator_pid=${navigator_pid:-unset}" >&2
    if [[ -n "${navigator_pid}" ]] && kill -0 "${navigator_pid}" 2>/dev/null; then
      echo "navigator_alive=yes" >&2
      ps -o pid=,ppid=,pgid=,stat=,comm=,args= -p "${navigator_pid}" >&2 || true
      if [[ -r "/proc/${navigator_pid}/task/${navigator_pid}/children" ]]; then
        children="$(<"/proc/${navigator_pid}/task/${navigator_pid}/children")"
        echo "module_children=${children:-none}" >&2
        if [[ -n "${children}" ]]; then
          ps -o pid=,ppid=,pgid=,stat=,comm=,args= --ppid "${navigator_pid}" >&2 || true
        fi
      fi
    else
      echo "navigator_alive=no" >&2
    fi
    if [[ -n "${socket_path:-}" ]]; then
      if [[ -S "${socket_path}" ]]; then
        echo "navigator_socket=present path=${socket_path}" >&2
      elif [[ -e "${socket_path}" ]]; then
        echo "navigator_socket=not-a-socket path=${socket_path}" >&2
      else
        echo "navigator_socket=missing path=${socket_path}" >&2
      fi
      echo "runtime_dir=${socket_path%/*}" >&2
      ls -la "${socket_path%/*}" >&2 || true
      "${navigator}" --command status --socket "${socket_path}" >&2 || true
    fi
  fi
  if [[ -n "${navigator_pid}" ]]; then
    kill "${navigator_pid}" 2>/dev/null || true
    wait "${navigator_pid}" 2>/dev/null || true
  fi
  if [[ "${status}" -ne 0 && -f "${work_dir}/navigator.log" ]]; then
    cat "${work_dir}/navigator.log" >&2
  fi
  rm -rf "${work_dir}"
}
trap cleanup EXIT

openssl genpkey -algorithm ED25519 -out "${ota_private_key}" >/dev/null 2>&1
openssl pkey -in "${ota_private_key}" -pubout -out "${ota_public_key}" >/dev/null 2>&1

make_package() {
  local package_root="$1"
  local version="$2"
  mkdir -p "${package_root}/release/bin" "${package_root}/config" \
    "${package_root}/deploy" "${package_root}/manifest"
  printf 'release %s\n' "${version}" >"${package_root}/release/bin/probe"
  install -m 0755 "${navigator}" "${package_root}/release/bin/cockpit-navigator"
  if [[ -x "$(dirname "${navigator}")/cockpit-ui" ]]; then
    install -m 0755 "$(dirname "${navigator}")/cockpit-ui" \
      "${package_root}/release/bin/cockpit-ui"
  fi
  mkdir -p "${package_root}/release/lib/cockpit/modules"
  cp -a "${module_dir}/." "${package_root}/release/lib/cockpit/modules/"
  printf 'system:\n  name: test\n' >"${package_root}/config/config.example.yaml"
  printf '%s\n' "${version}" >"${package_root}/manifest/VERSION"
  install -m 0755 "${source_root}/deploy/install.sh" \
    "${package_root}/deploy/install.sh"
  (
    cd "${package_root}"
    find release config deploy manifest -type f ! -name SHA256SUMS -print0 \
      | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
    openssl pkeyutl -sign -rawin -inkey "${ota_private_key}" \
      -in manifest/SHA256SUMS -out manifest/SHA256SUMS.sig
  )
}

install_root="${work_dir}/install"
mkdir -p "${install_root}/releases/1.0.0" "${install_root}/run"
ln -s releases/1.0.0 "${install_root}/current"

lock_ready="${work_dir}/lock-ready"
(
  exec 9>"${install_root}/run/safe-ota.lock"
  flock -n 9
  : >"${lock_ready}"
  sleep 1
) &
lock_pid=$!
while [[ ! -e "${lock_ready}" ]]; do
  sleep 0.01
done
set +e
"${safe_ota}" --recover --root "${install_root}"
lock_result=$?
set -e
wait "${lock_pid}"
[[ "${lock_result}" -eq 1 ]]
! grep -Fq 'ExecStartPre=' "${source_root}/deploy/systemd/cockpit-navigator.service"
grep -Fxq 'ReadWritePaths=/cockpit-system/data /cockpit-system/logs /cockpit-system/run' \
  "${source_root}/deploy/systemd/cockpit-navigator.service"
grep -Fq -- '--mode normal' \
  "${source_root}/deploy/systemd/cockpit-navigator.service"
grep -Fxq 'User=cockpit' \
  "${source_root}/deploy/systemd/cockpit-navigator.service"
grep -Fxq 'Group=cockpit' \
  "${source_root}/deploy/systemd/cockpit-navigator.service"

package_two="${work_dir}/package-2"
make_package "${package_two}" 2.0.0
set +e
"${safe_ota}" --package "${package_two}" --confirm 2.0.1 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/true --standalone
confirmation_result=$?
set -e
[[ "${confirmation_result}" -eq 2 ]]
[[ "$(readlink "${install_root}/current")" == "releases/1.0.0" ]]

"${safe_ota}" --package "${package_two}" --confirm 2.0.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/true --standalone
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]
[[ "$(<"${install_root}/data/ota-version-floor")" == "2.0.0" ]]

package_downgrade="${work_dir}/package-downgrade"
make_package "${package_downgrade}" 1.5.0
set +e
"${safe_ota}" --package "${package_downgrade}" --confirm 1.5.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/true --standalone
downgrade_result=$?
set -e
[[ "${downgrade_result}" -eq 1 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/releases/1.5.0" ]]

mkdir -p "${install_root}/releases/2.1.0"
printf 'state: prepared\nversion: 2.1.0\nprevious_release: releases/2.0.0\n' \
  >"${install_root}/run/upgrade-transaction.yaml"
"${safe_ota}" --recover --root "${install_root}"
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/releases/2.1.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]

mkdir -p "${install_root}/releases/2.2.0"
ln -sfn releases/2.2.0 "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
printf 'state: prepared\nversion: 2.2.0\nprevious_release: releases/2.0.0\n' \
  >"${install_root}/run/upgrade-transaction.yaml"
"${safe_ota}" --recover --root "${install_root}"
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/releases/2.2.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]

mkdir -p "${install_root}/releases/2.3.0"
ln -sfn releases/2.3.0 "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
printf 'state: activated\nversion: 2.3.0\nprevious_release: releases/2.0.0\n' \
  >"${install_root}/run/upgrade-transaction.yaml"
"${safe_ota}" --recover --root "${install_root}"
[[ "$(readlink "${install_root}/current")" == "releases/2.0.0" ]]
[[ ! -e "${install_root}/releases/2.3.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]

mkdir -p "${install_root}/releases/2.4.0"
ln -sfn releases/2.4.0 "${install_root}/current.new"
mv -Tf "${install_root}/current.new" "${install_root}/current"
printf 'state: confirmed\nversion: 2.4.0\nprevious_release: releases/2.0.0\n' \
  >"${install_root}/run/upgrade-transaction.yaml"
"${safe_ota}" --recover --root "${install_root}"
[[ "$(readlink "${install_root}/current")" == "releases/2.4.0" ]]
[[ -d "${install_root}/releases/2.4.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]

package_three="${work_dir}/package-3"
make_package "${package_three}" 3.0.0
set +e
"${safe_ota}" --package "${package_three}" --confirm 3.0.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/false --timeout 1 --standalone
health_result=$?
set -e
[[ "${health_result}" -eq 3 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.4.0" ]]
[[ ! -e "${install_root}/releases/3.0.0" ]]
[[ ! -e "${install_root}/run/upgrade-transaction.yaml" ]]

printf 'tampered\n' >>"${package_three}/release/bin/probe"
set +e
"${safe_ota}" --package "${package_three}" --confirm 3.0.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/true --standalone
checksum_result=$?
set -e
[[ "${checksum_result}" -eq 1 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.4.0" ]]

package_unsigned="${work_dir}/package-unsigned"
make_package "${package_unsigned}" 3.1.0
printf 'invalid signature\n' >"${package_unsigned}/manifest/SHA256SUMS.sig"
set +e
"${safe_ota}" --package "${package_unsigned}" --confirm 3.1.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command /bin/true --standalone
signature_result=$?
set -e
[[ "${signature_result}" -eq 1 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.4.0" ]]
[[ ! -e "${install_root}/releases/3.1.0" ]]

package_hanging="${work_dir}/package-hanging"
make_package "${package_hanging}" 3.2.0
hanging_health="${work_dir}/hanging-health.sh"
hanging_child_pid="${work_dir}/hanging-child.pid"
printf '#!/usr/bin/env bash\nsleep 60 &\necho "$!" >"%s"\nwait\n' \
  "${hanging_child_pid}" >"${hanging_health}"
chmod 0755 "${hanging_health}"
started_at="$(date +%s)"
set +e
"${safe_ota}" --package "${package_hanging}" --confirm 3.2.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --health-command "${hanging_health}" \
  --timeout 1 --standalone
hanging_health_result=$?
set -e
elapsed="$(( $(date +%s) - started_at ))"
[[ "${hanging_health_result}" -eq 3 ]]
[[ "${elapsed}" -lt 10 ]]
[[ "$(readlink "${install_root}/current")" == "releases/2.4.0" ]]
[[ -s "${hanging_child_pid}" ]]
! kill -0 "$(<"${hanging_child_pid}")" 2>/dev/null

mkdir -p "${install_root}/data/ota/incoming"
package_four="${install_root}/data/ota/incoming/package-4"
make_package "${package_four}" 4.0.0
install -m 0644 "${source_root}/configs/development.yaml" \
  "${install_root}/config/config.yaml"
sed -i "s|shared_memory_name: /cockpit_camera_preview|shared_memory_name: /cockpit_camera_ota_${$}|" \
  "${install_root}/config/config.yaml"
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
  --public-key "${ota_public_key}" --socket "${socket_path}" \
  --health-command /bin/true --timeout 10
[[ "$(readlink "${install_root}/current")" == "releases/4.0.0" ]]
[[ "$("${navigator}" --command mode --socket "${socket_path}")" == "OK mode=normal" ]]

package_five="${install_root}/data/ota/incoming/package-5"
make_package "${package_five}" 5.0.0
set +e
"${safe_ota}" --package "${package_five}" --confirm 5.0.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --socket "${socket_path}" \
  --health-command /bin/false --timeout 1
runtime_health_result=$?
set -e
[[ "${runtime_health_result}" -eq 3 ]]
[[ "$(readlink "${install_root}/current")" == "releases/4.0.0" ]]
[[ ! -e "${install_root}/releases/5.0.0" ]]
[[ "$("${navigator}" --command mode --socket "${socket_path}")" == "OK mode=normal" ]]
runtime_status="$("${navigator}" --command status --socket "${socket_path}")"
[[ "${runtime_status}" == *"module=transfer state=running"* ]]

package_never_ready="${install_root}/data/ota/incoming/package-never-ready"
make_package "${package_never_ready}" 6.0.0
replacement_started="${work_dir}/replacement-started"
mkfifo "${replacement_started}"
printf '#!/usr/bin/env bash\nprintf "ready\\n" >"%s"\nexec tail -f /dev/null\n' \
  "${replacement_started}" >"${package_never_ready}/release/bin/cockpit-navigator"
chmod 0755 "${package_never_ready}/release/bin/cockpit-navigator"
(
  cd "${package_never_ready}"
  find release config deploy manifest -type f ! -name SHA256SUMS ! -name SHA256SUMS.sig -print0 \
    | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
  openssl pkeyutl -sign -rawin -inkey "${ota_private_key}" \
    -in manifest/SHA256SUMS -out manifest/SHA256SUMS.sig
)
set +e
"${safe_ota}" --package "${package_never_ready}" --confirm 6.0.0 --root "${install_root}" \
  --public-key "${ota_public_key}" --socket "${socket_path}" \
  --health-command /bin/true --timeout 1 &
runtime_budget_pid=$!
replacement_signal="$(timeout 10 head -n 1 "${replacement_started}")"
started_at="$(date +%s)"
wait "${runtime_budget_pid}"
runtime_budget_result=$?
set -e
elapsed="$(( $(date +%s) - started_at ))"
[[ "${replacement_signal}" == "ready" ]]
[[ "${runtime_budget_result}" -ne 0 ]]
[[ "${elapsed}" -lt 5 ]]
[[ "$(readlink "${install_root}/current")" == "releases/4.0.0" ]]
[[ ! -e "${install_root}/releases/6.0.0" ]]

kill "${navigator_pid}" 2>/dev/null || true
wait "${navigator_pid}" 2>/dev/null || true
navigator_pid=""
