# Implementation Status

Scope: `system` is the Jetson-side smart cockpit system. Cloud backend and cloud frontend are deferred
until the local vehicle-side chain is stable.

Repository strategy: keep one `system` project for now; split backend/frontend/shared-proto only
after there is a real deployable boundary. See `docs/project_scope_and_repo_strategy.md` and
`docs/modularization_strategy.md`.

## Completed in this scaffold

- Top-level CMake project with Linux/WSL shell scripts.
- Common config, logging, runtime, time, and vehicle state helpers.
- Proto contract drafts for common, vehicle, gateway, and cloud.
- Runnable placeholders for:
  - `vehicle-data-service`
  - `cockpit-gateway-service`
  - `cloud-uplink-service`
  - `can-simulator`
  - `topic`
- Qt/QML and local Jetson debug dashboard placeholder directories.
- `configs/config.yaml`, `configs/logging.yaml`, and a systemd example.
- Old project analysis in `docs/reference_projects.md`.
- Modularization strategy based on `zelos/znavigator`: one main project, small internal modules,
  and future split-ready boundaries.
- Local file-backed topic debugging tool with `list`, `info`, `pub`, `echo`, and `hz` commands.

## Not implemented yet

- Real gRPC/protobuf code generation.
- SocketCAN read/write.
- MQTT client.
- Qt/QML gRPC client.
- WebSocket dashboard stream.
- GStreamer/V4L2 camera path.
- SQLite storage and recorder index.
- Shared memory ring buffer.
- Audio service for microphone/speaker.
- Voice interaction service for ASR/TTS/LLM orchestration.
- AI application layer for voice commands and large-model features.

## Verification

Verified in WSL2 / Ubuntu 22.04 on 2026-06-19.

Toolchain:

- CMake available.
- g++ 11.4.0.
- Ninja 1.10.1.

Commands:

```bash
bash scripts/install_ubuntu_deps.sh
bash scripts/build.sh
bash scripts/run_smoke.sh
```

Result:

- CMake configure and generate succeeded.
- Ninja build completed, including `can-simulator`.
- `ctest` passed `cockpit_smoke_test`.
- Smoke chain ran:
  - `can-simulator`
  - `vehicle-data-service`
  - `cockpit-gateway-service`
  - `cloud-uplink-service`

Current smoke output is still mock/stdout based. Real SocketCAN, gRPC, WebSocket, and MQTT
transports remain follow-up implementation work.
