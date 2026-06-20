# Implementation Status

Scope: `cockpit-system` is the Jetson-side smart cockpit system. Cloud backend and cloud frontend are deferred
until the local vehicle-side chain is stable.

Repository strategy: keep one `cockpit-system` project for now; split backend/frontend/shared-proto only
after there is a real deployable boundary. See `docs/project_scope_and_repo_strategy.md` and
`docs/modularization_strategy.md`.

## Completed in this scaffold

- Top-level CMake project with Linux/WSL shell scripts.
- Common config, logging, runtime, time, and vehicle state helpers.
- Common CAN frame model and SocketCAN RAII wrapper.
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
- CAN simulator with `stdout` and `socketcan` backends.

## Not implemented yet

- Real gRPC/protobuf code generation.
- SocketCAN integration in `vehicle-data-service`.
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

SocketCAN foundation re-verified from the new repository path on 2026-06-20 using the standard
`build/` directory.

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

Default smoke output remains mock/stdout based. The SocketCAN send backend is available but requires
`vcan0` or real CAN hardware. SocketCAN receive integration, gRPC, WebSocket, and MQTT remain
follow-up implementation work.
