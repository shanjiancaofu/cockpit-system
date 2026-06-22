# Implementation Status

Scope: `cockpit-system` is the Jetson-side smart cockpit system. Cloud backend and cloud frontend are deferred
until the local vehicle-side chain is stable.

Repository strategy: keep one `cockpit-system` project for now; split backend/frontend/shared-proto only
after there is a real deployable boundary. See `docs/project_scope_and_repo_strategy.md` and
`docs/modularization_strategy.md`.

## Completed in this scaffold

- Top-level CMake project with Linux/WSL shell scripts.
- Core config, logging, runtime, and time helpers.
- yaml-cpp `SystemConfig` with typed sections and startup validation.
- Vehicle state model, prototype CAN codec, and SocketCAN RAII wrapper.
- Platform-independent PCM16 format and RIFF/WAVE read/write module.
- ALSA PCM RAII driver and `audio-probe` list/capture/play diagnostic tool.
- Immutable 20 ms voice frames and a fixed-capacity lock-free SPSC ring buffer.
- Non-blocking ALSA poll capture and a threaded `AudioCaptureStream` with explicit state,
  xrun recovery, discontinuity flags, and runtime metrics.
- `audio-service` microphone capture owner with gRPC start/stop/status/metrics control plane.
- `audio-probe` remote control commands and ALSA `null` service smoke coverage.
- Stateful energy VAD worker with dBFS level, speech/silence transitions, metrics, and a replaceable
  `VoiceActivityDetector` boundary.
- Speech segment aggregation with pre-roll, endpoint flush, discontinuity/truncation flags, and a
  bounded local queue for the next ASR consumer.
- Mock ASR consumer with ordered transcript IDs, metrics, bounded replay history, and text-only
  server-streaming gRPC events.
- voice-interaction-service with deterministic allowlisted intents, ordered response events,
  status metrics, audio transcript subscription, and the voice-ctl gRPC diagnostic tool.
- Typed ActionDispatcher boundary with explicit success/rejected/not-implemented/failed results,
  mock allowlist execution, response fields, and execution metrics.
- Text-only audio Speak RPC, mock SpeechSynthesizer, bounded playback worker, ALSA AudioPlayer,
  audio/voice delivery metrics, and audio-probe speak diagnostics.
- Generated protobuf/gRPC contracts for common, vehicle, gateway, audio, and cloud.
- VehicleState server-streaming from `vehicle-data-service` to `cockpit-gateway-service`.
- CockpitEvent server-streaming from `cockpit-gateway-service` to local debug clients.
- Optional Qt 6/QML cockpit UI with a worker-thread gRPC client and UI-thread vehicle model.
- Qt UI freshness tracking with explicit live, stale, and disconnected states.
- Runnable placeholders for:
  - `vehicle-data-service`
  - `cockpit-gateway-service`
  - `cloud-uplink-service`
  - `can-simulator`
  - `topic`
- Qt 6/QML cockpit dashboard and local Jetson debug dashboard placeholder directory.
- Unified `configs/config.yaml` and a systemd example.
- Old project analysis in `docs/reference_projects.md`.
- Modularization strategy based on `zelos/znavigator`: one main project, small internal modules,
  and future split-ready boundaries.
- Topic debugging tool with file-backed commands and live gRPC `list/info/echo/hz` for
  `/vehicle/state`.
- CAN simulator with `stdout` and `socketcan` backends.
- `vehicle-data-service` with `mock` and `socketcan` sources.

## Not implemented yet

- Production vehicle CAN mapping from an approved DBC or signal specification.
- MQTT client.
- WebSocket dashboard stream.
- GStreamer/V4L2 camera path.
- SQLite storage and recorder index.
- Shared memory ring buffer.
- Real speech TTS provider and Jetson microphone/speaker calibration.
- Real ASR provider such as whisper.cpp or TensorRT-accelerated inference.
- WebRTC VAD backend and real microphone threshold calibration.
- AI application layer for voice commands and large-model features.

## Verification

Verified in WSL2 / Ubuntu 22.04, most recently on 2026-06-23.

SocketCAN foundation re-verified from the new repository path on 2026-06-20 using the standard
`build/` directory.

Toolchain:

- CMake available.
- g++ 11.4.0.
- Ninja 1.10.1.
- yaml-cpp 0.7.0.

Commands:

```bash
bash scripts/install_ubuntu_deps.sh
bash scripts/build.sh
bash scripts/run_smoke.sh
```

Result:

- CMake configure and generate succeeded.
- Ninja build completed, including generated protobuf/gRPC code.
- CTest 13/13 passed across audio, voice, config, vehicle, and UI model tests.
- Smoke chain ran:
  - `can-simulator`
  - `audio-service`
  - `voice-interaction-service`
  - `vehicle-data-service`
  - `cockpit-gateway-service`
  - `cloud-uplink-service`

Default smoke output remains mock/stdout based. `scripts/run_vcan_smoke.sh` provides an additional
`vcan0` send/receive path. The default smoke starts vehicle-data-service and gateway as separate
processes, verifies the upstream VehicleState stream, and verifies downstream topic discovery,
`echo`, and `hz` through CockpitEvent gRPC. WebSocket and MQTT remain follow-up implementation work.

The `vcan0` path was verified on 2026-06-21: `can-simulator` sent three prototype VehicleState
frames through SocketCAN and `vehicle-data-service` received and decoded all three successfully.
