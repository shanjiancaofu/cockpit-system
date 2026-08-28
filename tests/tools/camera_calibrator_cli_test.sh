#!/usr/bin/env bash
set -euo pipefail

calibrator="$1"

expect_status() {
  local expected="$1"
  shift
  set +e
  "$calibrator" "$@" >/dev/null 2>&1
  local actual=$?
  set -e
  if [[ "$actual" -ne "$expected" ]]; then
    echo "expected exit $expected, got $actual: $*" >&2
    exit 1
  fi
}

expect_status 0 --help
expect_status 2 --unknown
expect_status 2 --width invalid

fixture_dir="$(mktemp -d)"
trap 'rm -rf -- "$fixture_dir"' EXIT
printf 'P3\n2 2\n255\n0 0 0 0 0 0 0 0 0 0 0 0\n' >"$fixture_dir/01.ppm"
printf 'P3\n3 2\n255\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n' >"$fixture_dir/02.ppm"
expect_status 2 --input-dir "$fixture_dir"

echo "camera calibrator CLI tests passed"
