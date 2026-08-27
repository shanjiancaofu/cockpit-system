#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage:
  manage-ai-resource.sh stage RESOURCE_ROOT RELEASE
  manage-ai-resource.sh activate RESOURCE_ROOT
  manage-ai-resource.sh rollback RESOURCE_ROOT
  manage-ai-resource.sh status RESOURCE_ROOT

RESOURCE_ROOT must contain releases/RELEASE/MANIFEST.txt and the model file
named by model_file in that manifest. stage never activates a candidate.
EOF
  exit 2
}

[[ $# -ge 2 ]] || usage
command_name="$1"
resource_root="${2%/}"

if [[ -z "${resource_root}" || "${resource_root}" == "/" || -L "${resource_root}" ]]; then
  echo "RESOURCE_ROOT must be a non-symlink directory other than /" >&2
  exit 2
fi
mkdir -p "${resource_root}/releases"
resource_root="$(cd -- "${resource_root}" && pwd -P)"

exec 9>"${resource_root}/.resource.lock"
flock 9

valid_release() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ && "$1" != "latest" ]]
}

manifest_value() {
  local manifest="$1"
  local key="$2"
  sed -n "s/^${key}=//p" "${manifest}"
}

validate_release() {
  local release="$1"
  valid_release "${release}" || {
    echo "unsafe or unpinned AI resource release: ${release}" >&2
    return 1
  }
  local release_dir="${resource_root}/releases/${release}"
  local manifest="${release_dir}/MANIFEST.txt"
  [[ -d "${release_dir}" && ! -L "${release_dir}" && -f "${manifest}" ]] || {
    echo "AI resource release or manifest is missing: ${release}" >&2
    return 1
  }

  local key value
  local required=(family model_id model_file quantization sha256 runtime runtime_version provider_version config_version)
  for key in "${required[@]}"; do
    value="$(manifest_value "${manifest}" "${key}")"
    if [[ -z "${value}" || "$(grep -c "^${key}=" "${manifest}")" -ne 1 ]]; then
      echo "AI resource manifest requires exactly one non-empty ${key}" >&2
      return 1
    fi
  done

  local model_file expected_sha256
  model_file="$(manifest_value "${manifest}" model_file)"
  expected_sha256="$(manifest_value "${manifest}" sha256)"
  if [[ "${model_file}" == /* || "${model_file}" == *".."* || "${model_file}" == *$'\n'* ]]; then
    echo "AI resource model_file is unsafe: ${model_file}" >&2
    return 1
  fi
  if [[ ! "${expected_sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "AI resource manifest sha256 is invalid" >&2
    return 1
  fi
  local artifact="${release_dir}/${model_file}"
  [[ -f "${artifact}" && ! -L "${artifact}" ]] || {
    echo "AI resource model file is missing or is a symlink: ${model_file}" >&2
    return 1
  }
  printf '%s  %s\n' "${expected_sha256}" "${artifact}" | sha256sum --check --strict --status || {
    echo "AI resource model SHA-256 verification failed: ${release}" >&2
    return 1
  }
}

read_release_link() {
  local name="$1"
  local link="${resource_root}/${name}"
  if [[ ! -e "${link}" && ! -L "${link}" ]]; then
    return 1
  fi
  [[ -L "${link}" ]] || {
    echo "AI resource ${name} path is not a symlink" >&2
    return 2
  }
  local target
  target="$(readlink "${link}")"
  [[ "${target}" =~ ^releases/([A-Za-z0-9][A-Za-z0-9._-]*)$ ]] || {
    echo "AI resource ${name} target is unsafe: ${target}" >&2
    return 2
  }
  local release="${BASH_REMATCH[1]}"
  validate_release "${release}" >/dev/null
  printf '%s\n' "${release}"
}

set_release_link() {
  local name="$1"
  local release="$2"
  local temporary="${resource_root}/.${name}.new.$$"
  rm -f "${temporary}"
  ln -s "releases/${release}" "${temporary}"
  mv -Tf "${temporary}" "${resource_root}/${name}"
}

case "${command_name}" in
  stage)
    [[ $# -eq 3 ]] || usage
    release="$3"
    validate_release "${release}"
    set_release_link candidate "${release}"
    printf 'candidate=%s\n' "${release}"
    ;;
  activate)
    [[ $# -eq 2 ]] || usage
    candidate="$(read_release_link candidate)" || {
      echo "no valid candidate is staged" >&2
      exit 1
    }
    current=""
    if current="$(read_release_link current)"; then
      if [[ "${current}" != "${candidate}" ]]; then
        set_release_link previous "${current}"
      fi
    fi
    set_release_link current "${candidate}"
    printf 'current=%s\n' "${candidate}"
    [[ -z "${current}" || "${current}" == "${candidate}" ]] || printf 'previous=%s\n' "${current}"
    ;;
  rollback)
    [[ $# -eq 2 ]] || usage
    current="$(read_release_link current)" || {
      echo "no valid current release exists" >&2
      exit 1
    }
    previous="$(read_release_link previous)" || {
      echo "no valid previous release exists" >&2
      exit 1
    }
    [[ "${current}" != "${previous}" ]] || {
      echo "current and previous releases are identical" >&2
      exit 1
    }
    set_release_link candidate "${current}"
    set_release_link current "${previous}"
    set_release_link previous "${current}"
    printf 'current=%s\nprevious=%s\ncandidate=%s\n' "${previous}" "${current}" "${current}"
    ;;
  status)
    [[ $# -eq 2 ]] || usage
    for name in current previous candidate; do
      release=""
      if release="$(read_release_link "${name}")"; then
        printf '%s=%s\n' "${name}" "${release}"
      else
        printf '%s=none\n' "${name}"
      fi
    done
    ;;
  *)
    usage
    ;;
esac
