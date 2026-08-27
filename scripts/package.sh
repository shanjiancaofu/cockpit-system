#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"


cockpit_native_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "x86_64" ;;
    aarch64|arm64) echo "arm64" ;;
    *) echo "unsupported native architecture: $(uname -m)" >&2; return 1 ;;
  esac
}

cockpit_output_dir() { echo "${COCKPIT_OUTPUT_DIR:-${root_dir}/_output}"; }
cockpit_default_debug_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-debug"; }
cockpit_default_release_build_dir() { echo "$(cockpit_output_dir)/build/$(cockpit_native_arch)-release"; }
cockpit_default_runtime_dir() { echo "$(cockpit_output_dir)/runtime"; }


output_dir="$(cockpit_output_dir)"
build_dir="${BUILD_DIR:-$(cockpit_default_release_build_dir)}"
stage_dir="${STAGE_DIR:-${output_dir}/install/stage}"
dist_dir="${DIST_DIR:-${output_dir}/install/dist}"
package_info="${build_dir}/package-info.env"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
  echo "configured build directory not found: ${build_dir}" >&2
  echo "create it first with: bash ${root_dir}/scripts/build.sh --type release" >&2
  exit 1
fi

cmake -S "${root_dir}" -B "${build_dir}"

if [[ ! -f "${package_info}" ]]; then
  echo "CMake did not generate package metadata: ${package_info}" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${package_info}"

if [[ "${COCKPIT_BUILD_TYPE}" != "Release" &&
      "${ALLOW_NON_RELEASE_PACKAGE:-false}" != "true" ]]; then
  echo "package requires a Release build; found '${COCKPIT_BUILD_TYPE:-unknown}'" >&2
  exit 1
fi
if [[ "${COCKPIT_COMPILER_ID}" != "GNU" ]]; then
  echo "official Linux package requires GCC; found '${COCKPIT_COMPILER_ID:-unknown}'" >&2
  exit 1
fi

version="${VERSION:-${COCKPIT_VERSION}}"
if [[ ! "${version}" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
  echo "VERSION contains unsupported characters: '${version}'" >&2
  exit 2
fi
package_name="cockpit-system-${version}-${COCKPIT_PACKAGE_SYSTEM}-${COCKPIT_TARGET_ARCH}"
package_root="${stage_dir}/${package_name}"

rm -rf "${package_root}"
mkdir -p "${package_root}/release" "${package_root}/config" \
  "${package_root}/systemd" "${package_root}/deploy" "${package_root}/manifest" "${dist_dir}"

cmake --build "${build_dir}"
cmake --install "${build_dir}" --prefix "${package_root}/release" --component Runtime

install -m 0644 "${root_dir}/configs/production.yaml" \
  "${package_root}/config/config.example.yaml"
cp -a "${root_dir}/deploy/systemd/." "${package_root}/systemd/"
install -m 0755 "${root_dir}"/deploy/*.sh "${package_root}/deploy/"
install -m 0644 "${root_dir}/deploy/THIRD_PARTY_NOTICES.md" \
  "${package_root}/manifest/THIRD_PARTY_NOTICES.md"

printf '%s\n' "${version}" >"${package_root}/manifest/VERSION"

cat >"${package_root}/manifest/BUILD_INFO.json" <<EOF
{
  "project": "cockpit-system",
  "version": "${version}",
  "binary_version": "${COCKPIT_VERSION}",
  "git_revision": "${COCKPIT_GIT_REVISION}",
  "git_short": "${COCKPIT_GIT_SHORT}",
  "git_dirty": ${COCKPIT_GIT_DIRTY},
  "build_type": "${COCKPIT_BUILD_TYPE}",
  "build_time_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "system": "${COCKPIT_PACKAGE_SYSTEM}",
  "architecture": "${COCKPIT_TARGET_ARCH}",
  "build_machine_architecture": "$(uname -m)",
  "compiler": "${COCKPIT_COMPILER_PATH}",
  "compiler_id": "${COCKPIT_COMPILER_ID}",
  "compiler_version": "${COCKPIT_COMPILER_VERSION}",
  "protobuf_version": "${COCKPIT_PROTOBUF_VERSION}",
  "grpc_version": "${COCKPIT_GRPC_VERSION}"
}
EOF

(
  cd "${package_root}"
  find release config systemd deploy manifest \( -type f -o -type l \) \
    ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
)
if [[ -z "${OTA_SIGNING_KEY:-}" || ! -f "${OTA_SIGNING_KEY}" ]]; then
  echo "OTA_SIGNING_KEY must point to the release Ed25519 private key" >&2
  exit 1
fi
openssl pkeyutl -sign -rawin -inkey "${OTA_SIGNING_KEY}" \
  -in "${package_root}/manifest/SHA256SUMS" \
  -out "${package_root}/manifest/SHA256SUMS.sig"

archive="${dist_dir}/${package_name}.tar.gz"
tar -C "${stage_dir}" -czf "${archive}" "${package_name}"
echo "package created: ${archive}"
