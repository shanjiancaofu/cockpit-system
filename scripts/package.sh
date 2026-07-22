#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "${root_dir}/scripts/lib/build_paths.sh"

output_dir="$(cockpit_output_dir)"
build_dir="${BUILD_DIR:-$(cockpit_default_release_build_dir)}"
stage_dir="${STAGE_DIR:-${output_dir}/install/stage}"
dist_dir="${DIST_DIR:-${output_dir}/install/dist}"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
  echo "CMake build directory not found: ${build_dir}" >&2
  exit 1
fi

build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build_dir}/CMakeCache.txt")"
if [[ "${build_type}" != "Release" && "${ALLOW_NON_RELEASE_PACKAGE:-false}" != "true" ]]; then
  echo "package requires a Release build; found '${build_type:-unknown}'" >&2
  exit 1
fi

compiler_id="$(sed -n 's/^COCKPIT_COMPILER_ID:STRING=//p' "${build_dir}/CMakeCache.txt")"
if [[ "${compiler_id}" != "GNU" ]]; then
  echo "official Linux package requires a GCC Release build; found '${compiler_id:-unknown}'" >&2
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

sherpa_enabled="$(sed -n 's/^BUILD_SHERPA_ONNX_ASR:BOOL=//p' \
  "${build_dir}/CMakeCache.txt")"
protobuf_version=""
grpc_version=""
if [[ "${sherpa_enabled}" == "ON" ]]; then
  protobuf_cmake_dir="$(sed -n 's/^Protobuf_DIR:PATH=//p' \
    "${build_dir}/CMakeCache.txt")"
  grpc_cmake_dir="$(sed -n 's/^gRPC_DIR:PATH=//p' "${build_dir}/CMakeCache.txt")"
  if [[ -z "${protobuf_cmake_dir}" || -z "${grpc_cmake_dir}" ]]; then
    echo "SenseVoice package is missing Protobuf/gRPC CMake metadata" >&2
    exit 1
  fi

  protobuf_lib_dir="$(realpath "${protobuf_cmake_dir}/../..")"
  grpc_lib_dir="$(realpath "${grpc_cmake_dir}/../..")"
  if [[ "${protobuf_lib_dir}" != "${grpc_lib_dir}" ]]; then
    echo "SenseVoice package requires Protobuf and gRPC from one prefix" >&2
    exit 1
  fi
  if [[ ! -e "${protobuf_lib_dir}/libprotobuf.so.32" ||
        ! -e "${protobuf_lib_dir}/libgrpc++.so.1.51" ]]; then
    echo "expected Protobuf 3.21.12 and gRPC 1.51.3 runtime libraries were not found" >&2
    exit 1
  fi

  while IFS= read -r -d '' runtime_library; do
    cp -a "${runtime_library}" "${package_root}/release/lib/"
  done < <(find "${protobuf_lib_dir}" -maxdepth 1 \( -type f -o -type l \) \
    -name '*.so*' -print0)
  protobuf_version="3.21.12"
  grpc_version="1.51.3"
fi

install -m 0644 "${root_dir}/configs/config.yaml" \
  "${package_root}/config/config.example.yaml"
install -m 0644 "${root_dir}/configs/environment.example" \
  "${package_root}/config/environment.example"
cp -a "${root_dir}/configs/systemd/." "${package_root}/systemd/"
install -m 0755 "${root_dir}"/scripts/deploy/*.sh "${package_root}/deploy/"
install -m 0644 "${root_dir}/scripts/deploy/THIRD_PARTY_NOTICES.md" \
  "${package_root}/manifest/THIRD_PARTY_NOTICES.md"

printf '%s\n' "${version}" >"${package_root}/manifest/VERSION"

compiler="$(sed -n 's/^COCKPIT_COMPILER_PATH:FILEPATH=//p' "${build_dir}/CMakeCache.txt")"
compiler_version="$(sed -n 's/^COCKPIT_COMPILER_VERSION:STRING=//p' "${build_dir}/CMakeCache.txt")"
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
sherpa_dir="$(sed -n 's/^SHERPA_ONNX_DIR:PATH=//p' "${build_dir}/CMakeCache.txt")"
sherpa_commit=""
if [[ -n "${sherpa_dir}" && -d "${sherpa_dir}/.git" ]]; then
  sherpa_commit="$(git -C "${sherpa_dir}" rev-parse HEAD)"
fi
sensevoice_model_path="$(sed -n 's/^SHERPA_ONNX_SENSEVOICE_MODEL_PATH:FILEPATH=//p' \
  "${build_dir}/CMakeCache.txt")"
sensevoice_model_sha256=""
if [[ -n "${sensevoice_model_path}" && -f "${sensevoice_model_path}" ]]; then
  sensevoice_model_sha256="$(sha256sum "${sensevoice_model_path}" | awk '{print $1}')"
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
  "compiler_id": "${compiler_id}",
  "compiler_version": "${compiler_version}",
  "protobuf_version": "${protobuf_version}",
  "grpc_version": "${grpc_version}",
  "whisper_cpp_revision": "${whisper_commit}",
  "whisper_model_sha1": "${model_sha1}",
  "sherpa_onnx_revision": "${sherpa_commit}",
  "sensevoice_model_sha256": "${sensevoice_model_sha256}"
}
EOF

(
  cd "${package_root}"
  find release config systemd deploy manifest \( -type f -o -type l \) \
    ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum >manifest/SHA256SUMS
)

archive="${dist_dir}/${package_name}.tar.gz"
tar -C "${stage_dir}" -czf "${archive}" "${package_name}"
echo "package created: ${archive}"
