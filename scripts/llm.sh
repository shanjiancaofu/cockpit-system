#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -eq 0 ]]; then
  echo "do not run this script with sudo; it must create user-owned build and model files" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/.." && pwd)"
cd "${project_root}"

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
curl_metadata_options=("${curl_common_options[@]}" --silent)
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

llama_tag="b10456"
echo "[1/8] Resolving llama.cpp ${llama_tag}"
llama_revision="$(
  curl "${curl_metadata_options[@]}" \
    "https://api.github.com/repos/ggml-org/llama.cpp/commits/${llama_tag}" |
  jq -er '.sha'
)"

llama_source="$source_root/$llama_revision"

if [[ ! -d "$llama_source/.git" ]]; then
  git clone \
    --filter=blob:none \
    --depth 1 \
    --branch "$llama_tag" \
    https://github.com/ggml-org/llama.cpp.git \
    "$llama_source"
fi

test "$(git -C "$llama_source" rev-parse HEAD)" = "$llama_revision"

printf 'llama.cpp tag:      %s\n' "$llama_tag"
printf 'llama.cpp revision: %s\n' "$llama_revision"

# --------------------------------------------------
# 2. 查询并下载 Qwen3.5-2B Q4_K_M
# --------------------------------------------------

model_2b_repo="unsloth/Qwen3.5-2B-GGUF"
model_2b_metadata="$download_dir/qwen3.5-2b-metadata.json"

echo "[2/8] Resolving ${model_2b_repo}"
curl "${curl_metadata_options[@]}" \
  "${hf_endpoint}/api/models/${model_2b_repo}?blobs=true" \
  -o "$model_2b_metadata"

model_2b_revision="$(jq -er '.sha' "$model_2b_metadata")"

model_2b_count="$(
  jq '
    [
      .siblings[]
      | select(.rfilename | ascii_downcase | endswith("q4_k_m.gguf"))
    ] | length
  ' "$model_2b_metadata"
)"
test "$model_2b_count" -eq 1

model_2b_filename="$(
  jq -er '
    .siblings[]
    | select(.rfilename | ascii_downcase | endswith("q4_k_m.gguf"))
    | .rfilename
  ' "$model_2b_metadata"
)"

model_2b_sha256="$(
  jq -er --arg filename "$model_2b_filename" '
    .siblings[]
    | select(.rfilename == $filename)
    | (.lfs.sha256 // .lfs.oid)
  ' "$model_2b_metadata"
)"
model_2b_sha256="${model_2b_sha256#sha256:}"
model_2b_size="$(
  jq -er --arg filename "$model_2b_filename" '
    .siblings[]
    | select(.rfilename == $filename)
    | (.lfs.size // .size)
  ' "$model_2b_metadata"
)"

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
# 3. 查询并下载 Qwen3.5-4B Q4_K_M 对照模型
# --------------------------------------------------

model_4b_repo="unsloth/Qwen3.5-4B-GGUF"
model_4b_metadata="$download_dir/qwen3.5-4b-metadata.json"

echo "[3/8] Resolving ${model_4b_repo}"
curl "${curl_metadata_options[@]}" \
  "${hf_endpoint}/api/models/${model_4b_repo}?blobs=true" \
  -o "$model_4b_metadata"

model_4b_revision="$(jq -er '.sha' "$model_4b_metadata")"

model_4b_count="$(
  jq '
    [
      .siblings[]
      | select(.rfilename | ascii_downcase | endswith("q4_k_m.gguf"))
    ] | length
  ' "$model_4b_metadata"
)"
test "$model_4b_count" -eq 1

model_4b_filename="$(
  jq -er '
    .siblings[]
    | select(.rfilename | ascii_downcase | endswith("q4_k_m.gguf"))
    | .rfilename
  ' "$model_4b_metadata"
)"

model_4b_sha256="$(
  jq -er --arg filename "$model_4b_filename" '
    .siblings[]
    | select(.rfilename == $filename)
    | (.lfs.sha256 // .lfs.oid)
  ' "$model_4b_metadata"
)"
model_4b_sha256="${model_4b_sha256#sha256:}"
model_4b_size="$(
  jq -er --arg filename "$model_4b_filename" '
    .siblings[]
    | select(.rfilename == $filename)
    | (.lfs.size // .size)
  ' "$model_4b_metadata"
)"

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
QWEN_2B_SHA256=$model_2b_sha256
QWEN_4B_REPOSITORY=$model_4b_repo
QWEN_4B_REVISION=$model_4b_revision
QWEN_4B_FILENAME=$model_4b_filename
QWEN_4B_SHA256=$model_4b_sha256
EOF

cat "$ai_root/llm-versions.env"

# --------------------------------------------------
# 5. 编译 x86_64 CPU llama-server
# --------------------------------------------------

echo "[5/8] Building llama-server"
COCKPIT_LLAMA_CPP_REVISION="$llama_revision" \
COCKPIT_LLAMA_CPP_SOURCE_DIR="$llama_source" \
COCKPIT_LLAMA_CPP_CUDA=OFF \
bash scripts/prepare-llama-runtime.sh

# --------------------------------------------------
# 6. 安装生产和对照模型
# --------------------------------------------------

echo "[6/8] Installing verified model profiles"
COCKPIT_LLM_MODEL_PROFILE=production \
COCKPIT_LLM_MODEL_FILE="$model_2b_file" \
COCKPIT_LLM_MODEL_SHA256="$model_2b_sha256" \
bash scripts/prepare-llm-model.sh

COCKPIT_LLM_MODEL_PROFILE=comparison \
COCKPIT_LLM_MODEL_FILE="$model_4b_file" \
COCKPIT_LLM_MODEL_SHA256="$model_4b_sha256" \
bash scripts/prepare-llm-model.sh

# --------------------------------------------------
# 7. 检查 runtime
# --------------------------------------------------

echo "[7/8] Checking llama-server runtime dependencies"
ldd _output/ai/runtime/llama.cpp/current/bin/llama-server |
  tee _output/ai/llama-server-ldd.txt

# --------------------------------------------------
# 8. 依次运行 2B 和 4B 真实 smoke
# --------------------------------------------------

echo "[8/8] Running Qwen3.5-2B production smoke"
BUILD_DIR="${project_root}/_output/build/x86_64-stage11-debug" \
COCKPIT_LLM_MODEL_PROFILE=production \
bash scripts/tests/llama-server-smoke.sh

echo "[8/8] Running Qwen3.5-4B comparison smoke"
BUILD_DIR="${project_root}/_output/build/x86_64-stage11-debug" \
COCKPIT_LLM_MODEL_PROFILE=comparison \
COCKPIT_LLAMA_SERVER_PORT=18081 \
bash scripts/tests/llama-server-smoke.sh
