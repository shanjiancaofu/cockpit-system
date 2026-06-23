# Runtime Communication Strategy

This document records the communication strategy for the Jetson-side cockpit runtime. The goal is
to avoid both extremes: cloud-style over-service design and one large unstructured process.

## Decision

Use the smallest communication boundary that fits the latency, ownership, and deployment need:

```text
same thread
  -> function call

same process
  -> queue / actor mailbox / callback / SPSC ring

same machine, cross process
  -> Unix domain socket or shared memory + small notification message

cross machine or external tool
  -> TCP / gRPC / DDS-like middleware / MQTT / HTTP
```

For the current project phase, this means:

- Keep gRPC for control, status, debug tools, and low-rate typed events.
- Keep raw audio, future video frames, and other high-rate payloads out of gRPC.
- Prefer in-process queues and rings until there is a real cross-process requirement.
- Add shared memory only when large payloads must cross process boundaries.
- Do not introduce ROS 2, DDS, CyberRT, or a custom plugin ABI before the project needs that scale.

## Industry Reference

Autonomous-driving systems usually separate control messages from high-rate data paths.

```text
Apollo CyberRT:
  Publish/Subscribe + Shared Memory + Zero Copy

Autoware:
  ROS 2 + DDS, often with shared-memory optimization for same-machine traffic

Production self-developed runtime:
  Scheduler + MessageBus + SharedMemory + Recorder + Monitor
```

Large payloads such as images and point clouds normally do not travel as protobuf-over-TCP RPC
payloads. A common pattern is:

```text
Camera process
  -> writes image to shared memory
  -> publishes ImageReady { frame_id, timestamp, shm_handle }

Perception process
  -> receives ImageReady
  -> reads image bytes from shared memory
```

The message bus carries metadata and synchronization; the data plane carries the large buffer.

## Cockpit-System Layers

```text
Control plane:
  gRPC + CLI tools
  start / stop / status / config / debug / low-rate typed stream

Local event plane:
  queue / actor mailbox / future EventBus
  transcript events / intent events / UI events / vehicle status notifications

High-frequency data plane:
  SPSC ring / callback / future shared memory
  audio PCM / camera frames / sensor packets

External plane:
  MQTT / HTTP / WebSocket later
  cloud upload / browser dashboard / remote debugging
```

## Current Mapping

```text
audio-service:
  ALSA capture/playback ownership
  local AudioFrame SPSC ring
  gRPC control/status/transcript/Speak only

voice-interaction-service:
  text transcript and intent/action orchestration
  no ALSA and no raw PCM

vehicle-data-service:
  CAN or mock vehicle state source
  publishes vehicle state through typed interfaces

cockpit-gateway-service:
  aggregation boundary for UI/tools
  should not become a high-rate data tunnel
```

## Evolution Plan

### Stage 1: Current Jetson Project

```text
modules/*       -> domain logic
drivers/*       -> hardware adapters
services/*      -> daemon/node ownership where needed
proto/*         -> control/debug contracts
SPSC queues     -> local high-rate data
```

No generic runtime bus yet. Add simple typed queues where they remove direct coupling.
The first concrete primitive is `core/event/EventQueue<T>`, a bounded in-process queue for low-rate
events. It is intentionally mutex-backed and blocking-capable because it targets control and event
traffic, not audio/video hot paths.

### Stage 2: Small Runtime Layer

Add a small `core/event` only when multiple modules need the same low-rate event mechanism.

Possible shape:

```cpp
bus.Publish(event);
bus.Subscribe<EventType>(handler);
```

Initial implementation should stay in-process. It should not become a network middleware.

### Stage 3: Cross-Process High-Rate Data

Add `core/shm` only when camera/video or another large payload must cross process boundaries.

```text
small message -> event bus / Unix domain socket
large payload -> shared memory handle
```

### Stage 4: Recorder and Monitor

Add recorder and monitor after there are enough topics/events to justify them.

```text
Recorder:
  selected event metadata
  selected audio/video references or files

Monitor:
  process health
  queue depth
  drop count
  latency and timestamp checks
```

## Rules

- Do not create a new service only because a new feature exists.
- Create a daemon/node when it owns hardware, needs an independent lifecycle, or is used by multiple
  frontends/tools.
- Keep algorithms and domain behavior in `modules/*` until a deployment boundary is real.
- Do not send raw PCM, images, point clouds, or other large buffers through gRPC.
- Do not add shared memory before a callback/ring/queue becomes insufficient.
- Keep control APIs typed; avoid generic shell-command or string-command execution.
