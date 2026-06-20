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
core    # compatibility aggregate
```

Do not create separate repositories yet. The code should still be organized so that a module can
be moved to its own repository later with limited churn.

## Reference: zelos/znavigator

`zelos/znavigator` is useful as a modularization reference, not as a build-system template to copy.

Observed pattern:

```text
navigator/
  main.cc
  common/
  connection/
  run_config/
  util/
  title_edit/
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

Applied CMake pattern:

```text
common/CMakeLists.txt
  -> add_subdirectory(config)
  -> add_subdirectory(logging)
  -> add_subdirectory(runtime)
  -> add_subdirectory(utils)
  -> add_subdirectory(vehicle)
  -> add_subdirectory(can)

common/<module>/CMakeLists.txt
  -> source files
  -> direct target dependencies
```

Useful ideas for `cockpit-system`:

- Keep entry binaries thin.
- Put real behavior in internal modules.
- Give each module its own build target.
- Keep tests close to the module boundary.
- Express dependencies between modules explicitly in CMake.
- Avoid making every service directly depend on every shared helper.

Ideas not copied directly:

- Keep CMake instead of switching this project back to xmake.
- Do not introduce heavy package/build rules before the local Jetson chain is stable.
- Do not use long target names like `cockpit_common_runtime`.

## Target Shape

Short-term layout:

```text
cockpit-system/
  common/
    config/
    logging/
    runtime/
    utils/
    vehicle/
    can/
    audio/
    ai/
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
can      # SocketCAN and CAN frame helpers
core     # temporary compatibility aggregate
audio    # microphone/speaker capture and playback helpers
ai       # ASR/TTS/LLM adapters and orchestration helpers
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
  -> runtime/config/logging/utils
```

Rules:

- Every `common/<module>` directory owns its `CMakeLists.txt`.
- Every target declares direct dependencies instead of relying on global link state.
- `core` is only a compatibility aggregate and must not include `can`, `audio`, or `ai`.
- New binaries should link the smallest module targets they need.
- `can`, `audio`, and `ai` may depend on low-level modules, but not service code.
- `proto` should stay mostly independent; generated code can be linked by services and gateway.
- UI code must not access hardware modules directly.
- Hardware access belongs in service/tool modules.
- Service-to-service interaction should go through gateway/proto boundaries, not direct includes.

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
3. Put hardware probes and developer commands in `tools/<name>/`.
4. Add a smoke path before adding a full UI integration.
5. Keep target names short and readable.
