#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -eq 0 ]]; then
  echo "do not run this script with sudo; it must create user-owned build and model files" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/../.." && pwd)"
cd "${project_root}"
# shellcheck disable=SC1091
source "${script_dir}/llm-versions.sh"

case "$(uname -m)" in
  x86_64|amd64)
    native_arch=x86_64
    default_cuda=OFF
    default_cuda_architectures=""
    ;;
  aarch64|arm64)
    native_arch=arm64
    default_cuda=ON
    default_cuda_architectures=87
    ;;
  *)
    echo "unsupported native architecture: $(uname -m)" >&2
    exit 2
    ;;
esac

llama_cuda="${COCKPIT_LLAMA_CPP_CUDA:-${default_cuda}}"
if [[ "${llama_cuda}" == "ON" ]]; then
  llama_cuda_architectures="${COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES:-${default_cuda_architectures}}"
else
  llama_cuda_architectures=""
fi
llm_build_dir="${COCKPIT_LLM_BUILD_DIR:-${project_root}/_output/build/${native_arch}-debug}"

ai_root="${COCKPIT_AI_ROOT:-${project_root}/_output/ai}"
download_dir="$ai_root/downloads"
source_root="$ai_root/sources/llama.cpp"
hf_endpoint="${COCKPIT_HF_ENDPOINT:-https://huggingface.co}"
hf_endpoint="${hf_endpoint%/}"
curl_common_options=(
  --fail
  --location
  --show-error
  --retry 8
  --retry-all-errors
  --connect-timeout 30
)
curl_download_options=("${curl_common_options[@]}" --progress-bar)
mkdir -p "$download_dir" "$source_root"

download_verified() {
  local label="$1"
  local url="$2"
  local output="$3"
  local expected_sha256="$4"
  local expected_size="$5"
  local current_size=0

  if [[ -f "${output}" ]]; then
    current_size="$(stat -c '%s' "${output}")"
    if [[ "${current_size}" -eq "${expected_size}" ]]; then
      echo "${label} is already downloaded; verifying SHA-256"
      printf '%s  %s\n' "${expected_sha256}" "${output}" | sha256sum --check
      return
    fi
    if [[ "${current_size}" -gt "${expected_size}" ]]; then
      echo "${label} download is larger than expected: ${output}" >&2
      exit 1
    fi
    printf 'Resuming %s at %s of %s\n' "${label}" \
      "$(numfmt --to=iec --suffix=B "${current_size}")" \
      "$(numfmt --to=iec --suffix=B "${expected_size}")"
  fi

  curl "${curl_download_options[@]}" -C - "${url}" -o "${output}"
  echo "Verifying ${label} SHA-256"
  printf '%s  %s\n' "${expected_sha256}" "${output}" | sha256sum --check
}

# --------------------------------------------------
# 1. 固定 llama.cpp b10456 对应的精确 commit
# --------------------------------------------------

llama_tag="${LLAMA_CPP_TAG}"
llama_revision="${LLAMA_CPP_REVISION}"
echo "[1/8] Preparing pinned llama.cpp ${llama_tag} (${llama_revision})"

llama_source="$source_root/$llama_revision"

if [[ ! -d "$llama_source/.git" ]]; then
  mkdir -p "${llama_source}"
  git -C "${llama_source}" init
  git -C "${llama_source}" remote add origin https://github.com/ggml-org/llama.cpp.git
  git -C "${llama_source}" fetch --depth 1 origin "${llama_revision}"
  git -C "${llama_source}" checkout --detach FETCH_HEAD
fi

test "$(git -C "$llama_source" rev-parse HEAD)" = "$llama_revision"

printf 'llama.cpp tag:      %s\n' "$llama_tag"
printf 'llama.cpp revision: %s\n' "$llama_revision"

# --------------------------------------------------
# 2. 下载固定 Qwen3.5-2B Q4_K_M
# --------------------------------------------------

model_2b_repo="${QWEN_2B_REPOSITORY}"
model_2b_revision="${QWEN_2B_REVISION}"
model_2b_filename="${QWEN_2B_FILENAME}"
model_2b_sha256="${QWEN_2B_SHA256}"
model_2b_size="${QWEN_2B_SIZE}"

model_2b_file="$download_dir/$(basename "$model_2b_filename")"

printf '[2/8] Downloading %s (%s)\n' \
  "$model_2b_filename" "$(numfmt --to=iec --suffix=B "$model_2b_size")"
download_verified \
  "Qwen3.5-2B" \
  "${hf_endpoint}/${model_2b_repo}/resolve/${model_2b_revision}/${model_2b_filename}?download=true" \
  "$model_2b_file" \
  "$model_2b_sha256" \
  "$model_2b_size"

# --------------------------------------------------
# 3. 下载固定 Qwen3.5-4B Q4_K_M 对照模型
# --------------------------------------------------

model_4b_repo="${QWEN_4B_REPOSITORY}"
model_4b_revision="${QWEN_4B_REVISION}"
model_4b_filename="${QWEN_4B_FILENAME}"
model_4b_sha256="${QWEN_4B_SHA256}"
model_4b_size="${QWEN_4B_SIZE}"

model_4b_file="$download_dir/$(basename "$model_4b_filename")"

printf '[3/8] Downloading %s (%s)\n' \
  "$model_4b_filename" "$(numfmt --to=iec --suffix=B "$model_4b_size")"
download_verified \
  "Qwen3.5-4B" \
  "${hf_endpoint}/${model_4b_repo}/resolve/${model_4b_revision}/${model_4b_filename}?download=true" \
  "$model_4b_file" \
  "$model_4b_sha256" \
  "$model_4b_size"

# --------------------------------------------------
# 4. 保存完整来源和版本
# --------------------------------------------------

echo "[4/8] Writing version manifest"
cat >"$ai_root/llm-versions.env" <<EOF
LLAMA_CPP_TAG=$llama_tag
LLAMA_CPP_REVISION=$llama_revision
QWEN_2B_REPOSITORY=$model_2b_repo
QWEN_2B_REVISION=$model_2b_revision
QWEN_2B_FILENAME=$model_2b_filename
QWEN_2B_SIZE=$model_2b_size
QWEN_2B_SHA256=$model_2b_sha256
QWEN_4B_REPOSITORY=$model_4b_repo
QWEN_4B_REVISION=$model_4b_revision
QWEN_4B_FILENAME=$model_4b_filename
QWEN_4B_SIZE=$model_4b_size
QWEN_4B_SHA256=$model_4b_sha256
EOF

cat "$ai_root/llm-versions.env"

# --------------------------------------------------
# 5. Build the native llama-server runtime
# --------------------------------------------------

echo "[5/8] Building llama-server"
COCKPIT_LLAMA_CPP_REVISION="$llama_revision" \
COCKPIT_LLAMA_CPP_SOURCE_DIR="$llama_source" \
COCKPIT_LLAMA_CPP_CUDA="$llama_cuda" \
COCKPIT_LLAMA_CPP_CUDA_ARCHITECTURES="$llama_cuda_architectures" \
bash "${project_root}/scripts/ai/prepare-llama-runtime.sh"

# --------------------------------------------------
# 6. 安装生产和对照模型
# --------------------------------------------------

echo "[6/8] Installing verified model profiles"
COCKPIT_LLM_MODEL_PROFILE=production \
COCKPIT_LLM_MODEL_FILE="$model_2b_file" \
COCKPIT_LLM_MODEL_SHA256="$model_2b_sha256" \
bash "${project_root}/scripts/ai/prepare-llm-model.sh"

COCKPIT_LLM_MODEL_PROFILE=comparison \
COCKPIT_LLM_MODEL_FILE="$model_4b_file" \
COCKPIT_LLM_MODEL_SHA256="$model_4b_sha256" \
bash "${project_root}/scripts/ai/prepare-llm-model.sh"

# --------------------------------------------------
# 7. 检查 runtime
# --------------------------------------------------

echo "[7/8] Checking llama-server runtime dependencies"
runtime_bin_dir="${ai_root}/runtime/llama.cpp/current/bin"
server_bin="${runtime_bin_dir}/llama-server"
ldd_log="${ai_root}/llama-server-ldd.txt"
LD_LIBRARY_PATH="${runtime_bin_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  ldd "${server_bin}" | tee "${ldd_log}"
if grep -Fq 'not found' "${ldd_log}"; then
  echo "llama-server runtime dependency resolution failed" >&2
  exit 1
fi

# --------------------------------------------------
# 8. 依次运行 2B 和 4B 真实 smoke
# --------------------------------------------------

echo "[8/8] Running Qwen3.5-2B production smoke"
BUILD_DIR="${llm_build_dir}" \
COCKPIT_LLM_MODEL_PROFILE=production \
bash "${project_root}/scripts/tests/llama-server-smoke.sh"

echo "[8/8] Running Qwen3.5-4B comparison smoke"
BUILD_DIR="${llm_build_dir}" \
COCKPIT_LLM_MODEL_PROFILE=comparison \
COCKPIT_LLAMA_SERVER_PORT=18081 \
bash "${project_root}/scripts/tests/llama-server-smoke.sh"
