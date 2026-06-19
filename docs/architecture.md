# Architecture Snapshot

Source document: `../architecture_refined_v0.3.md`.

Scope and repository decision: see `docs/project_scope_and_repo_strategy.md`.

This folder implements the Jetson vehicle-side client from that document:

```text
vcan0 / can-simulator
  -> vehicle-data-service
  -> cockpit-gateway-service
  -> cockpit-ui / web-dashboard
  -> cloud-uplink-service
```

Current implementation boundary:

- C++17 + CMake is the main build path.
- This repository is the Jetson-side smart cockpit system. Cloud/backend/frontend projects are deferred
  until the local vehicle-side chain is stable.
- The project stays as one repository/folder for now. It should grow through internal CMake
  libraries and service modules, not early repository splitting.
- C++ source files use `.cc` consistently to match the zelos C++ repositories.
- gRPC/protobuf files are defined in `common/proto`, but C++ generation is deferred.
- YAML is represented by a lightweight parser so the scaffold can build without external packages.
- `vehicle-data-service`, `cockpit-gateway-service`, `cloud-uplink-service`, and
  `can-simulator` are runnable placeholders.
- Qt/QML, WebSocket, MQTT, GStreamer, WebRTC, SQLite, shared memory, audio, voice interaction, and
  AI integration remain explicit module boundaries for later phases.

Current directory shape:

```text
system/
  apps/                 # Qt/QML cockpit and local Jetson debug dashboard placeholders
  common/
    config/             # config file reader, later yaml-cpp
    logging/            # service logger
    proto/              # protobuf contracts
    runtime/            # args, config load, logger init, signal handling
    utils/              # low-level helpers
    vehicle/            # hand-written vehicle model before generated proto is enabled
  configs/              # runtime YAML and systemd examples
  docs/                 # architecture and old-project references
  services/             # long-running local services
  tools/                # developer and simulator binaries
  tests/                # smoke and unit tests
```

Planned voice/AI chain:

```text
microphone
  -> audio-service              # ALSA/PulseAudio/PipeWire capture, playback, volume
  -> voice-interaction-service  # wake word/PTT, ASR, dialog orchestration, TTS
  -> local model or remote LLM  # configurable provider
  -> cockpit-gateway-service
  -> cockpit-ui / speaker
```

Voice/AI scope for this Jetson project:

- Microphone input and speaker output are local hardware capabilities.
- `audio-service` owns audio devices; UI must not access ALSA/PulseAudio/PipeWire directly.
- `voice-interaction-service` owns ASR/TTS/LLM orchestration.
- LLM provider should be configurable:
  - local/offline model for demo without network
  - remote API for stronger reasoning or speech features
- The first version can use push-to-talk before wake word.
- Voice commands should call local service APIs, not shell commands directly.

Immediate next engineering tasks:

1. Add `common/can` with fd RAII and poll timeout, based on the transport-layer style audited in
   `docs/reference_code_audit.md`.
2. Let `can-simulator` support `stdout|socketcan` backends.
3. Let `vehicle-data-service` support `mock|socketcan` sources.
4. Replace the lightweight config reader with `yaml-cpp` and typed config structs.
5. Add protobuf and gRPC generation through CMake.
6. Implement `cockpit-gateway-service` streaming fan-out.
7. Add an audio hardware probe tool for microphone/speaker before implementing voice AI.
