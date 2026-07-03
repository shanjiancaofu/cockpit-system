# voice-interaction-service

The service directory follows the runtime responsibility rather than a generic `src/include`
layout:

```text
voice-interaction-service/
├── main.cc       process wiring and lifecycle
├── interaction/  transcript-to-response orchestration
├── audio/        audio-service transcript and speech clients
├── vehicle/      cockpit-gateway vehicle-status client
├── hmi/          typed Qt/Android handoff provider
└── grpc/         external control and status API
```

Each subdirectory owns an explicit CMake target. `main.cc` links those targets and contains no
domain implementation.

Consumes text transcripts from `audio-service`, maps them to allowlisted cockpit actions,
dispatches typed actions, and publishes response events with a separate execution status. It
never reads raw PCM and never executes shell commands.

The current assistant is deterministic. Actions use explicit local service clients behind the
`ActionDispatcher` boundary. Response text is sent to
`audio-service` through `Speak(text)`; this service never opens an ALSA device or transports PCM.

`query_vehicle_status` is the first real action. It queries the latest fresh snapshot from
`cockpit-gateway-service`. Camera and media actions currently use an explicit local HMI handoff
placeholder; they are not implemented as C++ media/UI features.

Recording/data-package capture is a developer diagnostic workflow and should be handled by a
separate recording boundary, not by user voice interaction.

Camera preview and media actions are user-facing HMI commands. They should be handed off to a future
Qt or Android bridge rather than implemented as a C++ media player inside this service.
The current runtime uses `LocalHmiCommandProvider`, which only records the handoff and returns a
clear placeholder response.
