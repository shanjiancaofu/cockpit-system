#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${root_dir}/scripts/lib/common.sh"


if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "Kokoro TTS stability baseline requires x86_64" >&2
  exit 2
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-sherpa-debug}"
iterations="${COCKPIT_KOKORO_STABILITY_ITERATIONS:-16}"

for required in \
  "${runtime_root}/lib/libsherpa-onnx-c-api.so" \
  "${runtime_root}/lib/libonnxruntime.so" \
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/model.onnx" \
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/voices.bin" \
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/tokens.txt" \
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/lexicon-us-en.txt" \
  "${ai_root}/models/tts/kokoro-multi-lang-v1_1/lexicon-zh.txt"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required Kokoro stability resource: ${required}" >&2
    exit 2
  fi
done
if [[ ! -d "${ai_root}/models/tts/kokoro-multi-lang-v1_1/espeak-ng-data" ]]; then
  echo "missing Kokoro espeak-ng-data directory" >&2
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

COCKPIT_AI_ROOT="${ai_root}" \
  LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${build_dir}/bin/sherpa_tts_stability_test" "${iterations}"
