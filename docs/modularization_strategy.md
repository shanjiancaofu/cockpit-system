# Modularization Strategy

This document records the current code organization decision for `cockpit-system`.

## Decision

Keep one main project:

```text
cockpit-system/
```

Use internal modules inside this project:

```text
config
logging
runtime
utils
vehicle
can
audio
ai
proto
```

Implemented internal targets:

```text
config
logging
runtime
utils
vehicle
can
```

Do not create separate repositories yet. The code should still be organized so that a module can
be moved to its own repository later with limited churn.

## Reference: zelos/znavigator

`zelos/znavigator` is useful as a modularization reference, not as a build-system template to copy.
From the directory shape, it should be treated as an application runtime, module orchestrator, and
process/plugin management shell rather than an autonomous-driving algorithm stack.

Observed pattern:

```text
navigator/
  main.cc
  common/
  connection/
  library/
    *_entry.cc
    *_entry.h
  run_config/
  util/
  title_edit/
  testdata/
  ...
```

The main binary depends on small internal targets:

```text
navigator
  -> navigator.common
  -> navigator.connection
  -> navigator.run_config
  -> navigator.util
  -> navigator.title_edit
```

Mapping for this project:

```text
znavigator/common       -> core/ plus small platform-independent primitives
znavigator/run_config   -> core/config and configs/*.yaml
znavigator/util         -> core/utils
znavigator/connection   -> proto plus service clients
znavigator/library/*    -> services/* as independently runnable capabilities
znavigator/testdata     -> tests/testdata when fixtures become necessary
znavigator/script       -> scripts
```

Architectural lessons:

- `library/*_entry` is an adapter pattern: the runtime talks to a stable entry surface, while the
  real business module can evolve behind it.
- `dl_api` suggests dynamic `.so` loading with `dlopen`/`dlsym`; this is useful for a mature product
  runtime, but it adds ABI, packaging, crash isolation, and deployment complexity.
- `common/zoe_*` suggests a uniform module model: module metadata, options, status, and operator
  actions such as start, stop, and status.
- `connection/ipc_connector` uses Unix `socketpair` file descriptors, `poll`, a fixed message
  header, and `ProtocolUnit` payloads. It is local runtime IPC, not a service-to-service gRPC API.
- `transfer/controller` and `transfer/restful` show a separate control plane for command, config,
  signal, and module description APIs.
- `application.yaml` contains channel maps and feature flags; it configures runtime wiring rather
  than only application business options.

Current cockpit-system stance:

- Use static CMake-linked services and typed gRPC control APIs first.
- Do not add a generic plugin loader or dynamic module ABI in the current Jetson phase.
- Keep the service entry surface explicit through `proto/` and service clients.
- Treat gRPC as a control/debug boundary. Do not push high-frequency audio/video/sensor data through
  gRPC just because two modules need to communicate.
- Prefer in-process queues, callbacks, SPSC rings, and later local IPC/shared memory for data paths.
- Add orchestration only when multiple independently deployed services need one launcher/control
  process. Until then, systemd plus smoke scripts are enough.

Applied CMake pattern:

```text
core/CMakeLists.txt
  -> add_subdirectory(config)
  -> add_subdirectory(logging)
  -> add_subdirectory(runtime)
  -> add_subdirectory(utils)

modules/CMakeLists.txt
  -> add_subdirectory(vehicle)
  -> add_subdirectory(can)
  -> add_subdirectory(audio)
  -> add_subdirectory(voice)

modules/voice/CMakeLists.txt
  -> add_subdirectory(asr)
  -> add_subdirectory(tts)
  -> add_subdirectory(assistant)
  -> add_subdirectory(actions)
  -> add_subdirectory(responses)

drivers/CMakeLists.txt
  -> add_subdirectory(socketcan)

<layer>/<module>/CMakeLists.txt
  -> source files
  -> direct target dependencies
```

Directory names describe concrete behavior. Avoid generic `base`, `common`, and `misc` buckets.
Do not add a child directory for one or two files unless it already represents a stable dependency
boundary. `can` and `vehicle` therefore remain flat, while voice is split because its ASR, TTS,
assistant, action, and response-output responsibilities evolve independently.

Useful ideas for `cockpit-system`:

- Keep entry binaries thin.
- Put real behavior in internal modules.
- Keep service entry files as lifecycle wiring, not as business logic containers.
- Use one small CMake target per module or service boundary.
- Give each module its own build target.
- Keep tests close to the module boundary.
- Express dependencies between modules explicitly in CMake.
- Avoid making every service directly depend on every shared helper.

Ideas not copied directly:

- Keep CMake instead of switching this project back to xmake.
- Do not introduce heavy package/build rules before the local Jetson chain is stable.
- Do not use long target names like `cockpit_common_runtime`.
- Do not introduce `dlopen` plugins before there is a real independent release and ABI boundary.
- Do not create a generic REST control server while gRPC tools already cover local debugging.

## Target Shape

Short-term layout:

```text
cockpit-system/
  core/
    config/
    logging/
    runtime/
    utils/
  modules/
    vehicle/
    can/
    audio/
    voice/
      asr/
      tts/
      assistant/
      actions/
      responses/
  drivers/
    socketcan/
    alsa/
  proto/
  services/
    vehicle-data-service/
    cockpit-gateway-service/
    audio-service/
    voice-interaction-service/
  tools/
    can-simulator/
    audio-probe/
  tests/
```

CMake targets:

```text
config   # runtime configuration
logging  # logging implementation
runtime  # service lifecycle; depends on config and logging
utils    # low-level helpers
vehicle  # base vehicle models; depends on utils
can      # platform-independent CAN frame model
socketcan # Linux SocketCAN adapter; depends on can
audio    # microphone/speaker capture and playback helpers
voice    # ASR/TTS/intent/action interfaces and orchestration helpers
voice_asr / voice_tts / voice_assistant / voice_actions / voice_responses
         # concrete voice responsibility targets aggregated by voice
proto    # protobuf contracts and generated code
```

Binary targets:

```text
vehicle-data-service
cockpit-gateway-service
audio-service
voice-interaction-service
can-simulator
audio-probe
```

## Dependency Rules

Default dependency direction:

```text
apps/services/tools
  -> required feature modules
  -> required platform drivers
  -> runtime/config/logging/utils
```

Rules:

- Every `core/<module>`, `modules/<module>`, and `drivers/<module>` owns its `CMakeLists.txt`.
- A large module may add responsibility subdirectories; each child owns a target and the parent may
  expose an INTERFACE aggregation target for compatibility.
- Every target declares direct dependencies instead of relying on global link state.
- `core/` is a directory category, not an umbrella CMake target.
- Binaries declare the smallest direct targets they use instead of linking all core libraries.
- New binaries should link the smallest module targets they need.
- `can`, `audio`, and `ai` may depend on core modules, but not service code.
- Platform-independent modules must not include Linux or hardware APIs directly.
- User-space hardware adapters belong in `drivers/<device>` and may depend on module data types.
- Kernel modules and device-tree sources stay under the matching driver directory and are opt-in.
- `proto` should stay mostly independent; generated code can be linked by services and gateway.
- UI code must not access hardware modules directly.
- Hardware access belongs in service/tool modules.
- Service-to-service interaction should go through gateway/proto boundaries, not direct includes.

## Communication Rules

Use different transport choices for different traffic:

```text
Same thread:
  function call

Same process:
  queue / actor mailbox / callback / SPSC ring

Same machine, cross process:
  Unix domain socket or shared memory + small notification message later

Control plane:
  gRPC / CLI tools
  start, stop, status, config, debug commands

Local event plane:
  in-process queue or typed event bus
  transcripts, intents, UI events, low-rate status changes

High-frequency data plane:
  SPSC ring, callback, shared memory later
  audio PCM, camera frames, sensor packets

External plane:
  MQTT / HTTP / WebSocket later
  cloud upload and browser dashboard
```

Rules:

- Do not model the Jetson system as ordinary cloud microservices.
- `services/*` means vehicle-side daemon/node/process, not a scalable web backend service.
- gRPC is allowed and useful, but it should mostly carry control/status and low-rate typed events.
- Raw PCM, image frames, and high-rate CAN/sensor streams should stay local and use ring buffers,
  callbacks, or future shared memory.
- A generic runtime message bus can be introduced later under `core/event` or `core/runtime`, but
  only after at least two modules need the same typed event mechanism.
- Shared memory should be introduced only when large payloads must cross process boundaries; small
  messages should carry metadata such as frame id, timestamp, and shared-memory handle.

## Placement Rules

Use these rules when adding the next feature:

```text
core/<name>/
```

Only for infrastructure that is not specific to vehicles, audio, voice, UI, cloud, or hardware:
configuration, logging, process lifecycle, time, string/path helpers, and generic storage helpers.

```text
modules/<name>/
```

For product/domain logic that should be testable without a long-running process. Examples:
vehicle state decoding, CAN frame codecs, audio frames, VAD, speech segmenting, ASR/TTS interfaces,
intent parsing, and action dispatch interfaces.

```text
drivers/<name>/
```

For Linux or Jetson hardware adapters. Examples: SocketCAN, ALSA, camera, GPIO, I2C, IIO, serial,
and later optional device-tree or kernel-side materials. Drivers adapt hardware APIs into module
interfaces; they should not contain voice, UI, or vehicle business policy.

```text
services/<name>/
```

For daemon-style runtime ownership: gRPC servers, threads, service metrics, device ownership,
reconnection, lifecycle, and integration between modules. Service `main.cc` should stay small and
mostly wire config, runtime, dependencies, and the service object.

```text
tools/<name>/
```

For developer commands, smoke helpers, simulators, and diagnostics. Tools may use drivers directly
when their purpose is probing hardware, but production flows should go through services.

## Future Split Rule

A module is ready to split into another repository only when all of these are true:

- It has a stable public API.
- It has its own tests or smoke tool.
- Its dependencies are explicit and small.
- It is useful outside the current `system` build.
- It has a real independent release or deployment reason.

Possible future splits:

```text
shared-proto
can-lib
audio-ai-lib
```

Do not split simply because the directory is large. Split only when the boundary is real.

## Current Practical Rule

For now, keep coding inside one project.

When adding a new feature:

1. Put generic reusable code in the smallest internal module.
2. Put long-running behavior in `services/<name>/`.
3. Put hardware access adapters in `drivers/<name>/`.
4. Put hardware probes and developer commands in `tools/<name>/`.
5. Add a smoke path before adding a full UI integration.
6. Keep target names short and readable.
