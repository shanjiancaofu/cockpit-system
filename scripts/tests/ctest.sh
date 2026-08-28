#!/usr/bin/env bash
set -euo pipefail

build_dir=""
arguments=("$@")
for ((index = 0; index < ${#arguments[@]}; ++index)); do
  case "${arguments[index]}" in
    --test-dir)
      if ((index + 1 < ${#arguments[@]})); then
        build_dir="${arguments[index + 1]}"
      fi
      ;;
    --test-dir=*) build_dir="${arguments[index]#--test-dir=}" ;;
  esac
done

if [[ -n "$build_dir" && -f "$build_dir/CMakeCache.txt" ]] &&
   grep -Fqx 'COCKPIT_ENABLE_TSAN:BOOL=ON' "$build_dir/CMakeCache.txt"; then
  if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "TSan CTest launcher currently supports Linux x86_64 only" >&2
    exit 2
  fi
  if ! command -v setarch >/dev/null 2>&1; then
    echo "warning: setarch is unavailable; running TSan with the host ASLR policy" >&2
    exec ctest "$@"
  fi
  # GCC 11 libtsan reserves a fixed shadow-memory range. Linux 6.8 ASLR can map
  # the PIE loader into that range before libtsan starts, producing a fatal
  # "unexpected memory mapping" before test code executes. Disable ASLR only
  # for the CTest process tree; ThreadSanitizer itself remains fully enabled.
  if setarch x86_64 -R true 2>/dev/null; then
    exec setarch x86_64 -R ctest "$@"
  fi
  echo "warning: this environment blocks setarch; running TSan with the host ASLR policy" >&2
  exec ctest "$@"
fi

exec ctest "$@"
