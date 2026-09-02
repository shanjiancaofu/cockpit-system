#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"


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
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/decoder-epoch-12-avg-2-chunk-16-left-64.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx"
  "${ai_root}/models/kws/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/tokens.txt"
  "${ai_root}/models/vad/silero-vad/silero_vad.onnx"
  "${ai_root}/models/asr/sensevoice-small-int8/model.int8.onnx"
  "${ai_root}/models/asr/sensevoice-small-int8/tokens.txt"
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/model.onnx"
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/voices.bin"
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/tokens.txt"
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/lexicon-us-en.txt"
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/lexicon-zh.txt"
  "${ai_root}/config/kws-keywords.txt"
  "${ai_root}/fixtures/nihao-xiaoshan.wav"
  "${ai_root}/fixtures/nihao-xiaoche.wav"
  "${ai_root}/fixtures/open-camera-zh.wav"
  "${ai_root}/fixtures/live/segment-02-wake-open-camera.wav"
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
  bash "${root_dir}/scripts/ai/prepare-sherpa-runtime.sh"
  bash "${root_dir}/scripts/ai/prepare-voice-models.sh"
  COCKPIT_KOKORO_TTS_SHA256=a3f4c73d043860e3fd2e5b06f36795eb81de0fc8e8de6df703245edddd87dbad \\
    bash "${root_dir}/scripts/ai/prepare-kokoro-tts.sh"

The smoke does not download resources during validation.
EOF
  exit 2
fi
if [[ ! -d "${ai_root}/models/tts/kokoro-multi-lang-v1_1/espeak-ng-data" ]]; then
  echo "Sherpa voice VM smoke is missing Kokoro espeak-ng-data directory" >&2
  exit 2
fi

if ! ldd_output="$(LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    ldd "${runtime_root}/lib/libsherpa-onnx-c-api.so")"; then
  echo "failed to inspect Sherpa runtime dependencies" >&2
  exit 1
fi
if grep -q "not found" <<<"${ldd_output}"; then
  echo "Sherpa runtime has unresolved dynamic library dependencies:" >&2
  printf '%s\n' "${ldd_output}" >&2
  exit 1
fi

BUILD_DIR="${build_dir}" bash "${root_dir}/scripts/build.sh" --arch x86_64 --type debug \
  --no-test -- \
  -DCOCKPIT_ENABLE_SHERPA_AGENT=ON \
  -DCOCKPIT_SHERPA_AGENT_RUNTIME_ROOT="${runtime_root}"

COCKPIT_AI_ROOT="${ai_root}" LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ctest --test-dir "${build_dir}" --output-on-failure \
    -R '^(sherpa_voice_smoke_test|sherpa_voice_gate_smoke_test|sherpa_tts_smoke_test|deterministic_command_router_test)$'

BUILD_DIR="${build_dir}" COCKPIT_AI_ROOT="${ai_root}" \
  COCKPIT_SHERPA_AGENT_RUNTIME_ROOT="${runtime_root}" \
  bash "${root_dir}/scripts/tests/sherpa-service-voice-smoke.sh"

echo "Sherpa voice VM smoke passed"
