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

- C++17 + CMake is the main build path.
- This repository is the Jetson-side smart cockpit system. Cloud/backend/frontend projects are deferred
  until the local vehicle-side chain is stable.
- The project stays as one repository/folder for now. It should grow through internal CMake
  libraries and service modules, not early repository splitting.
- C++ source files use `.cc` consistently to match the zelos C++ repositories.
- `proto` generates a C++ `contracts` target through CMake.
- yaml-cpp loads an immutable typed `SystemConfig` and validates component settings at startup.
- `vehicle-data-service` exposes a VehicleState gRPC stream consumed by `cockpit-gateway-service`.
- `cloud-uplink-service` remains a transport placeholder; `can-simulator` is runnable.
- ALSA microphone capture, local SPSC transport, and the audio gRPC control plane are implemented.
- `audio-service` owns the ring consumer and runs a replaceable energy-VAD implementation; PCM
  remains process-local while VAD state and metrics cross gRPC.
- Speech segmentation adds pre-roll and bounded duration, then publishes completed PCM segments
  to a local eight-entry SPSC queue for one ASR consumer.
- The ASR boundary lives in `modules/voice`; mock ASR currently consumes segments in-process and
  publishes text-only transcript events through gRPC with a bounded 32-event replay history.
- `audio-service` exclusively owns microphone and speaker devices. Its `Speak(text)` RPC runs
  mock TTS and bounded asynchronous ALSA playback without moving PCM between processes.
- `voice-interaction-service` owns intent/action orchestration and sends response text to
  `audio-service`; it never opens ALSA devices.
- Vehicle-status voice actions query the gateway's latest snapshot through a unary gRPC method.
  The gateway rejects missing or older-than-two-second state instead of serving stale data.
- Qt/QML, WebSocket, MQTT, GStreamer, WebRTC, SQLite, shared memory, real model providers, and
  broader AI integration remain explicit module boundaries for later phases.

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
  -> audio-service              # capture, VAD, segmentation, ASR, transcript events
  -> voice-interaction-service  # PTT, intent, dialog, LLM tools, response text
  -> audio-service              # TTS queue and speaker playback
  -> local model or remote LLM  # configurable provider
  -> cockpit-gateway-service
  -> cockpit-ui / speaker
```

Voice/AI scope for this Jetson project:

- Microphone input and speaker output are local hardware capabilities.
- `audio-service` owns audio devices; UI must not access ALSA/PulseAudio/PipeWire directly.
- `audio-service` owns in-process ASR because raw speech segments stay local.
- `voice-interaction-service` subscribes transcripts and owns intent/TTS/LLM orchestration.
- LLM provider should be configurable:
  - local/offline model for demo without network
  - remote API for stronger reasoning or speech features
- The first version can use push-to-talk before wake word.
- Voice commands should call local service APIs, not shell commands directly.

Immediate next engineering tasks:

1. Add ALSA poll/status results and the threaded `AudioCaptureStream` producer. Completed.
2. Build `audio-service` control/status APIs without sending raw PCM through gRPC. Completed.
3. Build mock transcript-to-intent handling in `voice-interaction-service`. Completed.
4. Add text-only Speak RPC, mock TTS, and asynchronous ALSA playback. Completed.
5. Decode production chassis frames after an approved DBC or signal specification is available.
