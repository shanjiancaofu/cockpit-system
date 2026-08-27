#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"


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


usage() {
  cat <<'EOF'
Usage: scripts/setup/prepare-jetson-sysroot.sh --source USER@JETSON [options]

Options:
  --source USER@JETSON  SSH destination for the Jetson
  --output DIRECTORY    Local sysroot directory (default: _output/sysroots/jetson)
  --with-cuda           Also copy /usr/local/cuda
  -h, --help            Show this help

The Jetson and the deployment target must use the same JetPack/L4T release.
EOF
}

source_host=""
output_dir="$(cockpit_output_dir)/sysroots/jetson"
with_cuda=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source)
      [[ $# -ge 2 ]] || { echo "--source requires a value" >&2; exit 2; }
      source_host="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || { echo "--output requires a value" >&2; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --with-cuda)
      with_cuda=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

[[ -n "${source_host}" ]] || { echo "--source is required" >&2; exit 2; }
command -v rsync >/dev/null 2>&1 || {
  echo "rsync not found; install it on both machines" >&2
  exit 1
}

mkdir -p \
  "${output_dir}/usr" \
  "${output_dir}/usr/lib" \
  "${output_dir}/usr/share/pkgconfig" \
  "${output_dir}/lib"

# Dereference target-side absolute links so the copied tree remains usable as a sysroot.
rsync -aL --delete "${source_host}:/usr/include/" "${output_dir}/usr/include/"
rsync -aL --delete "${source_host}:/usr/lib/aarch64-linux-gnu/" \
  "${output_dir}/usr/lib/aarch64-linux-gnu/"
rsync -aL --delete "${source_host}:/lib/aarch64-linux-gnu/" \
  "${output_dir}/lib/aarch64-linux-gnu/"
rsync -aL "${source_host}:/usr/share/pkgconfig/" "${output_dir}/usr/share/pkgconfig/"

if [[ "${with_cuda}" == true ]]; then
  mkdir -p "${output_dir}/usr/local"
  rsync -aL --delete "${source_host}:/usr/local/cuda/" "${output_dir}/usr/local/cuda/"
fi

cat >"${output_dir}/SYSROOT_INFO" <<EOF
source=${source_host}
created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
cuda_copied=${with_cuda}
EOF

echo "Jetson sysroot prepared at: ${output_dir}"
