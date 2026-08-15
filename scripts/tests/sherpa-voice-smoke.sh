#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${root_dir}/scripts/common.sh"

machine_arch="$(cockpit_native_arch)"
if [[ "${machine_arch}" != "x86_64" ]]; then
  echo "Sherpa voice VM smoke requires Ubuntu x86_64; current arch: ${machine_arch}" >&2
  exit 2
fi

if command -v lsb_release >/dev/null 2>&1; then
  distro_id="$(lsb_release -is)"
  distro_version="$(lsb_release -rs)"
  if [[ "${distro_id}" != "Ubuntu" || "${distro_version}" != 22.04* ]]; then
    echo "Sherpa voice VM smoke expects Ubuntu 22.04; current: ${distro_id} ${distro_version}" >&2
    exit 2
  fi
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
sherpa_version="${COCKPIT_SHERPA_ONNX_VERSION:-v1.13.4}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/${sherpa_version}}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-sherpa-debug}"

required_files=(
  "${runtime_root}/include/sherpa-onnx/c-api/c-api.h"
  "${runtime_root}/lib/libsherpa-onnx-c-api.so"
  "${runtime_root}/lib/libonnxruntime.so"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-8-left-64.int8.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-8-left-64.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-8-left-64.int8.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/en.phone"
  "${ai_root}/models/vad/silero-vad/silero_vad.onnx"
  "${ai_root}/models/asr/sensevoice-small-int8/model.int8.onnx"
  "${ai_root}/models/asr/sensevoice-small-int8/tokens.txt"
  "${ai_root}/config/kws-keywords.txt"
  "${ai_root}/fixtures/nihao-xiaoche.wav"
  "${ai_root}/fixtures/open-camera-zh.wav"
  "${ai_root}/fixtures/silence.wav"
)

missing=()
for required in "${required_files[@]}"; do
  if [[ ! -f "${required}" ]]; then
    missing+=("${required}")
  fi
done

if [[ ${#missing[@]} -ne 0 ]]; then
  echo "Sherpa voice VM smoke is missing required runtime/model/fixture files:" >&2
  printf '  %s\n' "${missing[@]}" >&2
  cat >&2 <<EOF

Prepare local resources first:
  bash scripts/prepare-sherpa-runtime.sh
  bash scripts/prepare-voice-models.sh

The smoke does not download resources during validation.
EOF
  exit 2
fi

if ldd "${runtime_root}/lib/libsherpa-onnx-c-api.so" | grep -q "not found"; then
  echo "Sherpa runtime has unresolved dynamic library dependencies:" >&2
  ldd "${runtime_root}/lib/libsherpa-onnx-c-api.so" >&2
  exit 1
fi

BUILD_DIR="${build_dir}" bash "${root_dir}/scripts/build.sh" --arch x86_64 --type debug \
  --no-test -- \
  -DCOCKPIT_ENABLE_SHERPA_AGENT=ON \
  -DCOCKPIT_SHERPA_AGENT_RUNTIME_ROOT="${runtime_root}"

COCKPIT_AI_ROOT="${ai_root}" LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ctest --test-dir "${build_dir}" --output-on-failure \
    -R '^(sherpa_voice_smoke_test|deterministic_command_router_test)$'

echo "Sherpa voice VM smoke passed"
