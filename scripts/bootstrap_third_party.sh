#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_dir="$(cd -- "${root_dir}/../.." && pwd)"
third_party_dir="${COCKPIT_THIRD_PARTY_DIR:-${workspace_dir}/third_party}"
build_jobs="${BUILD_JOBS:-$(nproc)}"

protobuf_revision="f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c"
grpc_revision="0879d3e2e96b5e51dac3c44ed05ddae7b172cd0f"
sherpa_revision="13d0ae6c539d2809d32f5eaa3ef1db0c459d0b24"
deps_root="${third_party_dir}/cockpit-unified-deps"
prefix="${deps_root}/prefix"
revision_marker="${prefix}/.cockpit-dependency-revisions"
expected_revisions="protobuf=${protobuf_revision}
grpc=${grpc_revision}
sherpa=${sherpa_revision}"

checkout_revision() {
  local url="$1"
  local revision="$2"
  local destination="$3"
  if [[ -d "${destination}/.git" ]]; then
    if [[ "$(git -C "${destination}" rev-parse HEAD)" != "${revision}" ]]; then
      echo "existing checkout has a different revision; refusing to overwrite: ${destination}" >&2
      return 1
    fi
    return
  fi
  git clone --filter=blob:none --no-checkout "${url}" "${destination}"
  git -C "${destination}" fetch --depth 1 origin "${revision}"
  git -C "${destination}" checkout --detach FETCH_HEAD
  [[ "$(git -C "${destination}" rev-parse HEAD)" == "${revision}" ]]
}

mkdir -p "${third_party_dir}" "${deps_root}"
sherpa_source="${third_party_dir}/sherpa-onnx"
if [[ -f "${revision_marker}" &&
      "$(<"${revision_marker}")" == "${expected_revisions}" &&
      -x "${prefix}/bin/protoc" &&
      -d "${sherpa_source}/.git" &&
      "$(git -C "${sherpa_source}" rev-parse HEAD)" == "${sherpa_revision}" ]]; then
  echo "pinned third-party dependencies are already ready in ${third_party_dir}"
  exit 0
fi

protobuf_source="${deps_root}/protobuf"
checkout_revision https://github.com/protocolbuffers/protobuf.git \
  "${protobuf_revision}" "${protobuf_source}"
cmake -S "${protobuf_source}/cmake" -B "${deps_root}/build-protobuf" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_SHARED_LIBS=ON
cmake --build "${deps_root}/build-protobuf" --parallel "${build_jobs}"
cmake --install "${deps_root}/build-protobuf"

grpc_source="${deps_root}/grpc"
checkout_revision https://github.com/grpc/grpc.git "${grpc_revision}" "${grpc_source}"
git -C "${grpc_source}" submodule update --init --recursive --depth 1
cmake -S "${grpc_source}" -B "${deps_root}/build-grpc" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_PREFIX_PATH="${prefix}" \
  -DBUILD_SHARED_LIBS=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_INSTALL=ON \
  -DgRPC_PROTOBUF_PROVIDER=package
cmake --build "${deps_root}/build-grpc" --parallel "${build_jobs}"
cmake --install "${deps_root}/build-grpc"

checkout_revision https://github.com/k2-fsa/sherpa-onnx.git \
  "${sherpa_revision}" "${sherpa_source}"
git -C "${sherpa_source}" submodule update --init --recursive --depth 1

"${prefix}/bin/protoc" --version
PKG_CONFIG_PATH="${prefix}/lib/pkgconfig" pkg-config --modversion grpc++
printf '%s\n' "${expected_revisions}" >"${revision_marker}"
echo "third-party dependencies are ready in ${third_party_dir}"
