#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

build_dir="${BUILD_DIR:-${root_dir}/$(cockpit_default_release_build_dir)}"
stage_dir="${STAGE_DIR:-${root_dir}/install/stage}"
dist_dir="${DIST_DIR:-${root_dir}/install/dist}"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
  echo "CMake build directory not found: ${build_dir}" >&2
  exit 1
fi

build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build_dir}/CMakeCache.txt")"
if [[ "${build_type}" != "Release" && "${ALLOW_NON_RELEASE_PACKAGE:-false}" != "true" ]]; then
  echo "package requires a Release build; found '${build_type:-unknown}'" >&2
  exit 1
fi

project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${build_dir}/CMakeCache.txt")"
version="${VERSION:-${project_version:-0.0.0}}"
git_revision="$(git -C "${root_dir}" rev-parse HEAD)"
git_short="$(git -C "${root_dir}" rev-parse --short HEAD)"
if ! git -C "${root_dir}" diff --quiet || ! git -C "${root_dir}" diff --cached --quiet; then
  git_dirty=true
else
  git_dirty=false
fi

target_system="$(sed -n 's/^COCKPIT_TARGET_SYSTEM:STRING=//p' "${build_dir}/CMakeCache.txt")"
architecture="$(sed -n 's/^COCKPIT_TARGET_ARCH:STRING=//p' "${build_dir}/CMakeCache.txt")"
if [[ -z "${target_system}" || -z "${architecture}" ]]; then
  echo "target system metadata missing; reconfigure ${build_dir} with the current CMake files" >&2
  exit 1
fi
system_name="$(printf '%s' "${target_system}" | tr '[:upper:]' '[:lower:]')"

package_name="cockpit-system-${version}-${system_name}-${architecture}"
package_root="${stage_dir}/${package_name}"

rm -rf "${package_root}"
mkdir -p "${package_root}/release/lib" "${package_root}/config" \
  "${package_root}/systemd" "${package_root}/deploy" "${package_root}/manifest" "${dist_dir}"

cmake --build "${build_dir}"
cmake --install "${build_dir}" --prefix "${package_root}/release" --component Runtime

install -m 0644 "${root_dir}/configs/config.yaml" \
  "${package_root}/config/config.example.yaml"
cp -a "${root_dir}/configs/systemd/." "${package_root}/systemd/"
install -m 0755 "${root_dir}"/scripts/deploy/*.sh "${package_root}/deploy/"
install -m 0644 "${root_dir}/scripts/deploy/THIRD_PARTY_NOTICES.md" \
  "${package_root}/manifest/THIRD_PARTY_NOTICES.md"

printf '%s\n' "${version}" >"${package_root}/manifest/VERSION"

compiler="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "${build_dir}/CMakeCache.txt")"
whisper_dir="$(sed -n 's/^WHISPER_CPP_DIR:PATH=//p' "${build_dir}/CMakeCache.txt")"
whisper_commit=""
if [[ -n "${whisper_dir}" && -d "${whisper_dir}/.git" ]]; then
  whisper_commit="$(git -C "${whisper_dir}" rev-parse HEAD)"
fi
model_path="$(sed -n 's/^WHISPER_CPP_MODEL_PATH:FILEPATH=//p' "${build_dir}/CMakeCache.txt")"
model_sha1=""
if [[ -n "${model_path}" && -f "${model_path}" ]]; then
  model_sha1="$(sha1sum "${model_path}" | awk '{print $1}')"
fi

cat >"${package_root}/manifest/BUILD_INFO.json" <<EOF
{
  "project": "cockpit-system",
  "version": "${version}",
  "git_revision": "${git_revision}",
  "git_short": "${git_short}",
  "git_dirty": ${git_dirty},
  "build_type": "${build_type}",
  "build_time_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "system": "${system_name}",
  "architecture": "${architecture}",
  "build_machine_architecture": "$(uname -m)",
  "compiler": "${compiler}",
  "whisper_cpp_revision": "${whisper_commit}",
  "whisper_model_sha1": "${model_sha1}"
}
EOF

(
  cd "${package_root}"
  find release config systemd deploy manifest -type f ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
)

archive="${dist_dir}/${package_name}.tar.gz"
tar -C "${stage_dir}" -czf "${archive}" "${package_name}"
echo "package created: ${archive}"
