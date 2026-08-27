#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf --one-file-system "${work_dir}"' EXIT
resource_root="${work_dir}/voice-model"
manager="${root_dir}/scripts/ai/manage-resource.sh"

make_release() {
  local release="$1"
  local content="$2"
  local release_dir="${resource_root}/releases/${release}"
  mkdir -p "${release_dir}"
  printf '%s\n' "${content}" >"${release_dir}/model.bin"
  local digest
  digest="$(sha256sum "${release_dir}/model.bin" | awk '{print $1}')"
  {
    printf 'family=test-asr\n'
    printf 'model_id=test/%s\n' "${release}"
    printf 'model_file=model.bin\n'
    printf 'quantization=int8\n'
    printf 'sha256=%s\n' "${digest}"
    printf 'runtime=sherpa-onnx\n'
    printf 'runtime_version=1.13.4\n'
    printf 'provider_version=1\n'
    printf 'config_version=1\n'
  } >"${release_dir}/MANIFEST.txt"
}

make_release 1.0.0 first
make_release 2.0.0 second

bash "${manager}" stage "${resource_root}" 1.0.0 >/dev/null
[[ ! -e "${resource_root}/current" ]]
bash "${manager}" activate "${resource_root}" >/dev/null
[[ "$(readlink "${resource_root}/current")" == releases/1.0.0 ]]

bash "${manager}" stage "${resource_root}" 2.0.0 >/dev/null
[[ "$(readlink "${resource_root}/current")" == releases/1.0.0 ]]
[[ "$(readlink "${resource_root}/candidate")" == releases/2.0.0 ]]
bash "${manager}" activate "${resource_root}" >/dev/null
[[ "$(readlink "${resource_root}/current")" == releases/2.0.0 ]]
[[ "$(readlink "${resource_root}/previous")" == releases/1.0.0 ]]

bash "${manager}" rollback "${resource_root}" >/dev/null
[[ "$(readlink "${resource_root}/current")" == releases/1.0.0 ]]
[[ "$(readlink "${resource_root}/previous")" == releases/2.0.0 ]]
[[ "$(readlink "${resource_root}/candidate")" == releases/2.0.0 ]]

make_release 3.0.0 third
printf 'corrupt\n' >>"${resource_root}/releases/3.0.0/model.bin"
if bash "${manager}" stage "${resource_root}" 3.0.0 >/dev/null 2>&1; then
  echo "resource manager accepted a corrupt candidate" >&2
  exit 1
fi
if bash "${manager}" stage "${resource_root}" latest >/dev/null 2>&1; then
  echo "resource manager accepted an unpinned latest release" >&2
  exit 1
fi

status="$(bash "${manager}" status "${resource_root}")"
grep -Fxq 'current=1.0.0' <<<"${status}"
echo "AI resource manager tests passed"
