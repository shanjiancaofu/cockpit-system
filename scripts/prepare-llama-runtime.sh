#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
revision="${COCKPIT_LLAMA_CPP_REVISION:-}"

if [[ -z "${revision}" ]]; then
  echo "COCKPIT_LLAMA_CPP_REVISION must name the reviewed llama.cpp commit" >&2
  exit 2
fi
if [[ ! "${revision}" =~ ^[0-9a-f]{7,40}$ ]]; then
  echo "COCKPIT_LLAMA_CPP_REVISION must be a 7-40 character hexadecimal commit" >&2
  exit 2
fi

runtime_dir="${ai_root}/runtime/llama.cpp/${revision}"
server_bin="${runtime_dir}/bin/llama-server"
if [[ -x "${server_bin}" ]]; then
  printf 'llama.cpp runtime already prepared: %s\n' "${runtime_dir}"
  exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${tmp_dir}"' EXIT
source_dir="${COCKPIT_LLAMA_CPP_SOURCE_DIR:-}"

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
  mkdir -p "${tmp_dir}/source"
  tar -xf "${source_archive}" -C "${tmp_dir}/source" --strip-components=1
  source_dir="${tmp_dir}/source"
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
fi

build_dir="${tmp_dir}/build"
cuda="${COCKPIT_LLAMA_CPP_CUDA:-OFF}"
cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_CURL=OFF \
  -DGGML_NATIVE=OFF \
  -DGGML_CUDA="${cuda}"
cmake --build "${build_dir}" --target llama-server --parallel "${JOBS:-$(nproc)}"

mkdir -p "${runtime_dir}/bin"
cp -a "${build_dir}/bin/." "${runtime_dir}/bin/"
if [[ ! -x "${server_bin}" ]]; then
  printf 'llama.cpp build did not produce %s\n' "${server_bin}" >&2
  exit 1
fi

{
  printf 'revision=%s\n' "${revision}"
  printf 'cuda=%s\n' "${cuda}"
  printf 'compiler=%s\n' "$(c++ --version | head -n 1)"
} >"${runtime_dir}/MANIFEST.txt"

printf 'llama.cpp runtime prepared: %s\n' "${runtime_dir}"
