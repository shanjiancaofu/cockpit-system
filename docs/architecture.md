# Architecture Snapshot

Source document: `docs/architecture_refined_v0.3.md`.

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

- C++17 or newer with CMake is the main build path; C++17 is the default baseline.
- This repository is the Jetson-side smart cockpit system. Cloud/backend/frontend projects are deferred
  until the local vehicle-side chain is stable.
- The project stays as one repository/folder for now. It should grow through internal CMake
  libraries and service modules, not early repository splitting.
- C++ source files use `.cc` consistently to match the zelos C++ repositories.
- `proto` generates a C++ `contracts` target through CMake.
- yaml-cpp loads an immutable typed `SystemConfig` and validates component settings at startup.
- `vehicle-data-service` exposes a VehicleState gRPC stream consumed by `cockpit-gateway-service`.
- `cloud-uplink-service` remains a transport placeholder; `can-simulator` is runnable.
- Qt/QML, WebSocket, MQTT, GStreamer, WebRTC, SQLite, shared memory, audio, voice interaction, and
  AI integration remain explicit module boundaries for later phases.

Current directory shape:

```text
cockpit-system/
  apps/                 # Qt/QML cockpit and local Jetson debug dashboard placeholders
  core/
    config/             # config file reader, later yaml-cpp
    logging/            # service logger
    runtime/            # args, config load, logger init, signal handling
    utils/              # low-level helpers
  modules/
    audio/              # PCM/WAV types, voice frames, local SPSC data plane
    can/                # platform-independent CAN frame model
    vehicle/            # hand-written vehicle model before generated proto is enabled
  drivers/
    alsa/               # Linux ALSA capture/playback adapter
    socketcan/          # Linux SocketCAN adapter
  proto/                # protobuf contracts
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

1. Add ALSA poll/status results and the threaded `AudioCaptureStream` producer.
2. Build `audio-service` control/status APIs without sending raw PCM through gRPC.
3. Decode production chassis frames after an approved DBC or signal specification is available.
