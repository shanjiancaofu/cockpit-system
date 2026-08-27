#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${work_dir}"' EXIT
bundle_root="${work_dir}/bundle/sherpa-runtime"
mkdir -p "${bundle_root}/include/sherpa-onnx/c-api" "${bundle_root}/lib"
printf '/* test Sherpa C API */\n' >"${bundle_root}/include/sherpa-onnx/c-api/c-api.h"
printf 'Apache License test fixture\n' >"${bundle_root}/LICENSE"

system_library="$(ldd /bin/sh | awk '/libc\.so/{print $3; exit}')"
[[ -f "${system_library}" ]]
for library in libsherpa-onnx-c-api.so libsherpa-onnx-cxx-api.so libonnxruntime.so; do
  cp "${system_library}" "${bundle_root}/lib/${library}"
done

archive="${work_dir}/sherpa-runtime.tar.gz"
tar -czf "${archive}" -C "${work_dir}/bundle" sherpa-runtime
digest="$(sha256sum "${archive}" | awk '{print $1}')"
ai_root="${work_dir}/ai"

COCKPIT_AI_ROOT="${ai_root}" \
COCKPIT_SHERPA_RUNTIME_ARCHIVE="${archive}" \
COCKPIT_SHERPA_RUNTIME_SHA256="${digest}" \
  bash "${root_dir}/scripts/ai/prepare-sherpa-runtime.sh" >/dev/null

runtime="${ai_root}/runtime/sherpa-onnx/v1.13.4"
[[ -f "${runtime}/LICENSE" ]]
grep -Fxq "archive_sha256=${digest}" "${runtime}/MANIFEST.txt"
[[ "$(readlink "${ai_root}/runtime/sherpa-onnx/current")" == v1.13.4 ]]

# A second run must reuse the verified runtime without requiring its source archive.
COCKPIT_AI_ROOT="${ai_root}" bash "${root_dir}/scripts/ai/prepare-sherpa-runtime.sh" >/dev/null

printf 'tampered\n' >>"${runtime}/LICENSE"
if COCKPIT_AI_ROOT="${ai_root}" bash "${root_dir}/scripts/ai/prepare-sherpa-runtime.sh" \
    >/dev/null 2>&1; then
  echo "Sherpa preparation reused a runtime with a modified license" >&2
  exit 1
fi

echo "Sherpa runtime preparation tests passed"
