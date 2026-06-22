# Project Scope and Repository Strategy

This document records the current scope decision for `cockpit-system`.

## Current Project Goal

`cockpit-system` is the Jetson-side smart cockpit project.

The first goal is not to build a full cloud platform. The first goal is to build a usable local
Jetson vehicle smart cockpit system:

```text
Jetson hardware
  -> local services
  -> cockpit gateway
  -> Qt/QML cockpit UI
  -> local logs/config/status
```

The project may later add a local data backend and Web dashboard, but they are not part of the
current first-stage scope.

## Repository Decision

Current decision:

```text
Use one repository/folder: cockpit-system
Do not create backend/frontend repositories yet
Do not split common modules into separate external libraries yet
```

Internal modularization follows `docs/modularization_strategy.md`, using `zelos/znavigator` as a
reference for a thin main binary plus small internal build targets.

Reason:

- There is currently one Jetson board and one main deliverable.
- Splitting repositories too early increases build, dependency, and debugging cost.
- The core vehicle-side chain is not complete yet.
- Local services still need to share config, logging, runtime, proto drafts, and test helpers.

Recommended current shape:

```text
cockpit-system/
  apps/                 # Qt/QML cockpit UI and local debug dashboard
  core/                 # process-independent infrastructure
  modules/              # reusable domain and product capabilities
  drivers/              # Linux and hardware adapters
  proto/                # service contracts
  configs/              # runtime config and systemd examples
  docs/                 # architecture, reference notes, plans
  services/             # long-running local services
  tools/                # simulator, probe, developer tools
  tests/                # smoke/unit tests
```

## Internal Libraries

The project should still use internal CMake libraries. This keeps code modular without creating
separate repositories.

Current internal libraries:

```text
config
logging
runtime
utils
vehicle
can
socketcan
```

Planned internal libraries:

```text
audio   # microphone, speaker, capture/playback helpers
ai      # ASR, TTS, LLM provider adapters and orchestration helpers
proto   # protobuf contracts and generated code
```

These should stay in the same repository until there is a strong reason to split them.
The names are intentionally short because the repository and directory paths already provide the
cockpit/system context.

## When To Split Repositories

Do not split now. Split later only if at least one condition is true:

- A local backend is actually needed to store history in SQLite.
- A Web dashboard becomes a separate deployable product.
- The Qt cockpit UI and backend have different release cycles.
- The frontend has a large Node/Vite dependency tree that slows vehicle-side development.
- A shared protocol package is used by multiple independently built projects.
- Another person or team works on the backend/frontend independently.

Possible future shape:

```text
project/
  cockpit-system/       # Jetson cockpit client
  local-server/         # optional local data backend
  web-dashboard/        # optional browser dashboard
  shared-proto/         # optional shared protocol package
```

This is a future option, not the current plan.

## Usability

The architecture must stay usable on one Jetson board.

Rules:

- Every phase should produce something runnable.
- Tools should have smoke commands.
- Config should have safe defaults.
- Logs should be easy to find.
- Hardware access should have mock mode and real mode.
- First version of each hardware feature should include a probe tool.

Examples:

```text
tools/can-simulator
tools/audio-probe
services/vehicle-data-service --source mock
services/vehicle-data-service --source socketcan
```

## Feasibility

The project must remain feasible for a single-developer Jetson project.

Rules:

- Use C++17 + CMake for the vehicle-side mainline.
- Add heavy dependencies only when a runnable chain needs them.
- Use mock providers before real hardware/provider integrations.
- Avoid building full cloud, account, permission, OTA, and multi-vehicle features in the first
  stage.
- Prefer local hardware capabilities before remote/cloud dependency.

Phased dependency plan:

```text
Stage 1: C++17, CMake, SocketCAN, local logs/config
Stage 2: Qt/QML, protobuf/gRPC
Stage 3: V4L2/GStreamer, SQLite
Stage 4: ALSA/PulseAudio/PipeWire, voice mock pipeline
Stage 5: real ASR/TTS/LLM provider
```

## Extensibility

Extensibility should come from clear module boundaries, not early repository splitting.

Rules:

- UI does not access hardware directly.
- Services own hardware and system interfaces.
- `cockpit-gateway-service` aggregates and throttles data for UI.
- Large data such as video/audio frames should not be pushed through gRPC as raw payloads.
- Every service has a README describing responsibility, input, output, config, and startup.
- Protocol files live under `proto` until shared externally.

Target service boundary:

```text
vehicle-data-service      # CAN, vehicle state, sensors
camera-service            # V4L2/GStreamer camera
media-service             # music/video playback
audio-service             # microphone/speaker
voice-interaction-service # ASR/TTS/LLM orchestration
ai-assistant-service      # intent/tool dispatch, optional later
cockpit-gateway-service   # UI aggregation
```

## Open Source and Old Code Reference Strategy

Reference code is used to guide structure, not copied blindly.

Reference dimensions:

- build system
- directory layout
- service lifecycle
- config loading
- logging
- hardware abstraction
- protocol boundaries
- tests and smoke tools
- deployment scripts

Local old-code references:

- `../vehicle-system`: Qt vehicle UI migration, process bridge, logging.
- `../zelos/zcarcloud`: service lifecycle, config reload, transfer handlers.
- `../zelos/car_cloud_server`: API shape, status storage, checksum rules.
- `../zelos/safe_ota`: HTTP/protobuf client and token refresh patterns.
- legacy hardware demos: V4L2/ioctl sequence and sensor access order.

Open-source project references should be used for architecture ideas only:

- Qt/QML dashboard projects: UI page organization and model/view separation.
- Linux SocketCAN examples: CAN socket setup and frame loop.
- GStreamer examples: Jetson camera/audio pipeline shape.
- ROS/Autoware-style projects: sensor separation and runtime launch discipline.
- Home Assistant-style projects: local-first device integration thinking.

Do not import large frameworks just because they are popular. The project stays Jetson-local and
demo-oriented until the core chain is stable.

## Current Answer

Does the project need to split into several repositories now?

```text
No.
```

Does the project need internal libraries/modules?

```text
Yes.
```

Current strategy:

```text
One repository/folder.
Many internal CMake targets.
Split external repositories later only when there is a real deployable boundary.
```

Detailed module boundary rules are recorded in `docs/modularization_strategy.md`.
