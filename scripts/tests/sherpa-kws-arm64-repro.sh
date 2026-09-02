#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"
runtime_root="${COCKPIT_SHERPA_AGENT_RUNTIME_ROOT:-${ai_root}/runtime/sherpa-onnx/v1.13.4}"
build_dir="${BUILD_DIR:-${root_dir}/_output/build/arm64-full-system-release}"
archive="${COCKPIT_SHERPA_RUNTIME_ARCHIVE:-${ai_root}/downloads/sherpa-onnx-v1.13.4-runtime.tar.gz}"
model_dir="${ai_root}/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20"
keywords="${ai_root}/config/kws-keywords.txt"
fixture="${ai_root}/fixtures/nihao-xiaoshan.wav"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Sherpa KWS ARM64 reproduction requires an aarch64 host" >&2
  exit 2
fi
for required in \
  "${runtime_root}/lib/libsherpa-onnx-c-api.so" \
  "${archive}" \
  "${model_dir}/encoder-epoch-13-avg-2-chunk-8-left-64.int8.onnx" \
  "${model_dir}/decoder-epoch-13-avg-2-chunk-8-left-64.onnx" \
  "${model_dir}/joiner-epoch-13-avg-2-chunk-8-left-64.int8.onnx" \
  "${model_dir}/tokens.txt" \
  "${keywords}" \
  "${fixture}" \
  "${build_dir}/bin/sherpa_voice_smoke_test"; do
  [[ -f "${required}" ]] || {
    echo "missing reproduction input: ${required}" >&2
    exit 2
  }
done

work_dir="$(mktemp -d /tmp/cockpit-sherpa-kws-repro.XXXXXX)"
cleanup() {
  rm -rf --one-file-system "${work_dir}"
}
trap cleanup EXIT

tar -xf "${archive}" -C "${work_dir}" --wildcards \
  '*/bin/sherpa-onnx-keyword-spotter'
official_cli="$(find "${work_dir}" -type f -name sherpa-onnx-keyword-spotter -print -quit)"
[[ -x "${official_cli}" ]] || chmod 0755 "${official_cli}"

set +e
LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${official_cli}" \
  --tokens="${model_dir}/tokens.txt" \
  --encoder="${model_dir}/encoder-epoch-13-avg-2-chunk-8-left-64.int8.onnx" \
  --decoder="${model_dir}/decoder-epoch-13-avg-2-chunk-8-left-64.onnx" \
  --joiner="${model_dir}/joiner-epoch-13-avg-2-chunk-8-left-64.int8.onnx" \
  --provider=cpu --num-threads=1 \
  --keywords-file="${keywords}" --keywords-score=3 \
  --keywords-threshold=0.1 --num-trailing-blanks=1 \
  "${fixture}" >"${work_dir}/official-cli.log" 2>&1
cli_result=$?

COCKPIT_AI_ROOT="${ai_root}" \
LD_LIBRARY_PATH="${runtime_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${build_dir}/bin/sherpa_voice_smoke_test" --kws "${fixture}" \
  >"${work_dir}/project-c-api.log" 2>&1
c_api_result=$?
set -e

cat "${work_dir}/official-cli.log"
cat "${work_dir}/project-c-api.log"

cli_detected=false
if grep -Fq '"keyword": "你好小山"' "${work_dir}/official-cli.log"; then
  cli_detected=true
fi
if [[ "${cli_result}" -eq 0 && "${cli_detected}" == true && "${c_api_result}" -ne 0 ]]; then
  echo "REPRODUCED: official CLI detects 你好小山; project C API provider does not"
  exit 0
fi
echo "NOT REPRODUCED: cli_result=${cli_result} cli_detected=${cli_detected} c_api_result=${c_api_result}" >&2
exit 1
