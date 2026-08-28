#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
revision="${COCKPIT_LLAMA_CPP_REVISION:-}"
source_sha256="${COCKPIT_LLAMA_CPP_SOURCE_SHA256:-}"
runtime_arch="$(uname -m)"
cuda="${COCKPIT_LLAMA_CPP_CUDA:-OFF}"
cuda_architectures="${COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES:-}"

if [[ -z "${revision}" ]]; then
  echo "COCKPIT_LLAMA_CPP_REVISION must name the reviewed llama.cpp commit" >&2
  exit 2
fi
if [[ ! "${revision}" =~ ^[0-9a-f]{7,40}$ ]]; then
  echo "COCKPIT_LLAMA_CPP_REVISION must be a 7-40 character hexadecimal commit" >&2
  exit 2
fi
if [[ "${cuda}" != "ON" && "${cuda}" != "OFF" ]]; then
  echo "COCKPIT_LLAMA_CPP_CUDA must be ON or OFF" >&2
  exit 2
fi
if [[ "${cuda}" == "OFF" && -n "${cuda_architectures}" ]]; then
  echo "COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES requires COCKPIT_LLAMA_CPP_CUDA=ON" >&2
  exit 2
fi
if [[ "${cuda}" == "ON" ]] && ! command -v nvcc >/dev/null 2>&1; then
  echo "CUDA llama.cpp build requires nvcc in PATH" >&2
  exit 2
fi

runtime_dir="${ai_root}/runtime/llama.cpp/${revision}"
server_bin="${runtime_dir}/bin/llama-server"
if [[ -x "${server_bin}" ]]; then
  manifest="${runtime_dir}/MANIFEST.txt"
  if [[ -f "${manifest}" ]] &&
     grep -Fxq "revision=${revision}" "${manifest}" &&
     grep -Fxq "arch=${runtime_arch}" "${manifest}" &&
     grep -Fxq "cuda=${cuda}" "${manifest}" &&
     grep -Fxq "cuda_architectures=${cuda_architectures}" "${manifest}"; then
    ln -sfn "${revision}" "${ai_root}/runtime/llama.cpp/current"
    printf 'llama.cpp runtime already prepared: %s\n' "${runtime_dir}"
    exit 0
  fi
  printf 'existing llama.cpp runtime does not match requested arch/CUDA profile; rebuilding: %s\n' \
    "${runtime_dir}" >&2
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${tmp_dir}"' EXIT
source_dir="${COCKPIT_LLAMA_CPP_SOURCE_DIR:-}"
source_is_verified_archive=false

if [[ -z "${source_dir}" ]]; then
  source_archive="${COCKPIT_LLAMA_CPP_SOURCE_ARCHIVE:-}"
  source_url="${COCKPIT_LLAMA_CPP_SOURCE_URL:-}"
  if [[ -z "${source_archive}" && -n "${source_url}" ]]; then
    source_archive="${tmp_dir}/llama.cpp-source.tar.gz"
    curl -fL --retry 3 --connect-timeout 20 -o "${source_archive}" "${source_url}"
  fi
  if [[ -z "${source_archive}" ]]; then
    cat >&2 <<EOF
llama.cpp source is required. Provide one of:
  COCKPIT_LLAMA_CPP_SOURCE_DIR=/path/to/llama.cpp
  COCKPIT_LLAMA_CPP_SOURCE_ARCHIVE=/path/to/llama.cpp.tar.gz
  COCKPIT_LLAMA_CPP_SOURCE_URL=https://...
EOF
    exit 2
  fi
  if [[ ! "${source_sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "COCKPIT_LLAMA_CPP_SOURCE_SHA256 must be the reviewed source archive SHA-256" >&2
    exit 2
  fi
  printf '%s  %s\n' "${source_sha256}" "${source_archive}" | sha256sum --check --status || {
    echo "llama.cpp source archive SHA-256 verification failed" >&2
    exit 1
  }
  mkdir -p "${tmp_dir}/source"
  tar -xf "${source_archive}" -C "${tmp_dir}/source" --strip-components=1
  source_dir="${tmp_dir}/source"
  source_is_verified_archive=true
fi

if [[ ! -f "${source_dir}/CMakeLists.txt" ]]; then
  printf 'llama.cpp source root is invalid: %s\n' "${source_dir}" >&2
  exit 2
fi
if [[ -d "${source_dir}/.git" ]]; then
  actual_revision="$(git -C "${source_dir}" rev-parse HEAD)"
  expected_revision="$(git -C "${source_dir}" rev-parse "${revision}^{commit}")"
  if [[ "${actual_revision}" != "${expected_revision}" ]]; then
    printf 'llama.cpp source HEAD %s does not match requested revision %s\n' \
      "${actual_revision}" "${revision}" >&2
    exit 2
  fi
elif [[ "${source_is_verified_archive}" != true ]]; then
  echo "COCKPIT_LLAMA_CPP_SOURCE_DIR must be a Git checkout so its commit can be verified" >&2
  exit 2
fi

build_dir="${COCKPIT_LLAMA_CPP_BUILD_DIR:-}"
if [[ -z "${build_dir}" ]]; then
  if [[ -d "${source_dir}/.git" ]]; then
    backend=cpu
    if [[ "${cuda}" == "ON" ]]; then
      backend="cuda-${cuda_architectures:-default}"
      backend="${backend//;/_}"
    fi
    build_dir="${ai_root}/build/llama.cpp/${revision}-${backend}"
  else
    build_dir="${tmp_dir}/build"
  fi
fi
build_dir="$(realpath -m "${build_dir}")"
cmake_args=(
  -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_CURL=OFF \
  -DGGML_NATIVE=OFF \
  -DGGML_CUDA="${cuda}"
)
if [[ -n "${cuda_architectures}" ]]; then
  cmake_args+=("-DCMAKE_CUDA_ARCHITECTURES=${cuda_architectures}")
fi
cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --target llama-server --parallel "${JOBS:-$(nproc)}"

if [[ -e "${runtime_dir}/bin" ]]; then
  rm -rf --one-file-system "${runtime_dir}/bin"
fi
mkdir -p "${runtime_dir}/bin"
cp -a "${build_dir}/bin/." "${runtime_dir}/bin/"
if [[ ! -x "${server_bin}" ]]; then
  printf 'llama.cpp build did not produce %s\n' "${server_bin}" >&2
  exit 1
fi

{
  printf 'revision=%s\n' "${revision}"
  if [[ -n "${source_sha256}" ]]; then
    printf 'source_sha256=%s\n' "${source_sha256,,}"
  fi
  printf 'arch=%s\n' "${runtime_arch}"
  printf 'cuda=%s\n' "${cuda}"
  printf 'cuda_architectures=%s\n' "${cuda_architectures}"
  printf 'compiler=%s\n' "$(c++ --version | head -n 1)"
} >"${runtime_dir}/MANIFEST.txt"

ln -sfn "${revision}" "${ai_root}/runtime/llama.cpp/current"

printf 'llama.cpp runtime prepared: %s\n' "${runtime_dir}"
