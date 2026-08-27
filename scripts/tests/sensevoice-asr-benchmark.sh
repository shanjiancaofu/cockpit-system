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


if [[ "$(cockpit_native_arch)" != "x86_64" ]]; then
  echo "SenseVoice ASR benchmark requires x86_64" >&2
  exit 2
fi

ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
build_dir="${BUILD_DIR:-$(cockpit_output_dir)/build/x86_64-sherpa-debug}"
repetitions="${COCKPIT_SENSEVOICE_BENCHMARK_REPETITIONS:-3}"

required_files=(
  "${runtime_root}/lib/libsherpa-onnx-c-api.so"
  "${runtime_root}/lib/libonnxruntime.so"
  "${ai_root}/models/asr/sensevoice-small-int8/model.int8.onnx"
  "${ai_root}/models/asr/sensevoice-small-int8/tokens.txt"
  "${ai_root}/fixtures/open-camera-zh.wav"
  "${ai_root}/fixtures/open-camera-en.wav"
  "${ai_root}/fixtures/live/segment-02-wake-open-camera.wav"
  "${ai_root}/fixtures/live/segment-03-multi-command.wav"
  "${ai_root}/fixtures/live/segment-04-vehicle-status.wav"
  "${ai_root}/fixtures/live/segment-05-mixed-commands.wav"
  "${ai_root}/fixtures/live/segment-06-negative-commands.wav"
  "${ai_root}/fixtures/live/segment-07-stop.wav"
)
for required in "${required_files[@]}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required SenseVoice benchmark resource: ${required}" >&2
    exit 2
  fi
done

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
  "${build_dir}/bin/sherpa_asr_benchmark_test" "${repetitions}"
