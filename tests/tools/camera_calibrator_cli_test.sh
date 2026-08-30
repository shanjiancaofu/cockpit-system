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

help_output="$($calibrator --help)"
grep -Fq -- '--board-profile NAME' <<<"$help_output"
grep -Fq -- 'q12-70-5' <<<"$help_output"
grep -Fq -- '--input-video FILE' <<<"$help_output"
grep -Fq -- '--near-distance N' <<<"$help_output"
grep -Fq -- '--far-distance N' <<<"$help_output"
grep -Fq -- '--tilt-threshold N' <<<"$help_output"
expect_status 2 --unknown
expect_status 2 --width invalid
expect_status 2 --board-profile unknown
expect_status 2 --board-profile q12-70-5 --corners-x 9
expect_status 2 --board-profile q12-70-5 --corners-y 6
expect_status 2 --board-profile q12-70-5 --square-size 0.025
expect_status 2 --board-profile q12-70-5 --width invalid
expect_status 2 --board-profile q12-70-5 --near-distance invalid
expect_status 2 --board-profile q12-70-5 --far-distance invalid
expect_status 2 --board-profile q12-70-5 --tilt-threshold invalid
expect_status 2 --input-video /does/not/exist.mp4
expect_status 2 --input-dir /tmp --input-video /tmp/test.mp4

fixture_dir="$(mktemp -d)"
trap 'rm -rf -- "$fixture_dir"' EXIT
printf 'P3\n2 2\n255\n0 0 0 0 0 0 0 0 0 0 0 0\n' >"$fixture_dir/01.ppm"
printf 'P3\n3 2\n255\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n' >"$fixture_dir/02.ppm"
expect_status 2 --input-dir "$fixture_dir"

echo "camera calibrator CLI tests passed"
