#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ai_root="${COCKPIT_AI_ROOT:-${root_dir}/_output/ai}"

if [[ -z "${COCKPIT_COMMAND_FIXTURE:-}" ]]; then
  echo "COCKPIT_COMMAND_FIXTURE must point to a verified play_music recording" >&2
  echo "The existing _output/ai/fixtures/live/play-music.wav is currently labeled incorrectly" >&2
  exit 2
fi

COCKPIT_COMMAND_FIXTURE="${COCKPIT_COMMAND_FIXTURE}" \
COCKPIT_EXPECTED_ACTION=play_music \
COCKPIT_MEDIA_FOCUS_SMOKE=true \
COCKPIT_MEDIA_MANIFEST="${COCKPIT_MEDIA_MANIFEST:-${root_dir}/_output/media/manifest.yaml}" \
COCKPIT_SERVICE_VOICE_REPETITIONS="${COCKPIT_SERVICE_VOICE_REPETITIONS:-3}" \
bash "${root_dir}/scripts/tests/sherpa-service-voice-smoke.sh"
