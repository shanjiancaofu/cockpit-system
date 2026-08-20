#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${root_dir}/scripts/common.sh"

output_dir="$(cockpit_output_dir)"
apply_changes=false
retention_days=7
archive_stamp="$(date -u +%Y%m%dT%H%M%SZ)"

usage() {
  cat <<'EOF'
Usage: scripts/cleanup-output.sh [options]

Review generated output without deleting files. Moves are performed only with --apply.

Options:
  --output-dir DIR       Override the output root (default: _output or COCKPIT_OUTPUT_DIR)
  --retention-days DAYS  Archive smoke/stability run directories older than DAYS (default: 7)
  --apply                Perform the listed moves; no files are deleted
  -h, --help             Show this help
EOF
}

while (($# > 0)); do
  case "$1" in
    --output-dir)
      [[ $# -ge 2 ]] || { echo "--output-dir requires a value" >&2; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --retention-days)
      [[ $# -ge 2 && "$2" =~ ^[0-9]+$ ]] || {
        echo "--retention-days requires a non-negative integer" >&2
        exit 2
      }
      retention_days="$2"
      shift 2
      ;;
    --apply)
      apply_changes=true
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

if [[ ! -d "${output_dir}" ]]; then
  echo "output directory does not exist: ${output_dir}" >&2
  exit 1
fi
output_dir="$(cd -- "${output_dir}" && pwd)"

build_dir="${output_dir}/build"
runtime_dir="${output_dir}/runtime"
build_archive="${build_dir}/archive/${archive_stamp}"
run_archive="${runtime_dir}/reports/archive/${archive_stamp}/run"

legacy_builds=(
  legacy
  vscode-Debug
  x86_64-llm-default-debug
  x86_64-llm-release
  x86_64-stage11-debug
)

move_or_report() {
  local source="$1"
  local destination="$2"
  if [[ ! -e "${source}" ]]; then
    return
  fi
  if [[ "${apply_changes}" == true ]]; then
    mkdir -p "$(dirname -- "${destination}")"
    mv -- "${source}" "${destination}"
    echo "archived: ${source} -> ${destination}"
  else
    echo "would archive: ${source} -> ${destination}"
  fi
}

if [[ -d "${build_dir}" ]]; then
  for name in "${legacy_builds[@]}"; do
    move_or_report "${build_dir}/${name}" "${build_archive}/${name}"
  done
fi

if [[ -d "${runtime_dir}/run" ]]; then
  while IFS= read -r -d '' path; do
    move_or_report "${path}" "${run_archive}/$(basename -- "${path}")"
  done < <(
    find "${runtime_dir}/run" -mindepth 1 -maxdepth 1 -type d \
      \( -name 'smoke-*' -o -name 'stability-*' \) -mtime "+${retention_days}" -print0
  )
fi

echo "output cleanup review complete: ${output_dir}"
if [[ "${apply_changes}" != true ]]; then
  echo "dry-run only; pass --apply to archive listed directories"
else
  echo "archive moves complete; no files were deleted"
fi
