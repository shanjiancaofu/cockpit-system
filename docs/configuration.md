# Configuration Architecture

## Reference

The configuration design references
`/home/ffz/code/project/zelos/zcarcloud/zcarcloud/carcloud/conf/z-car-cloud.yaml` and its loading
path through `CarCloud::InitEnv` and `ConfigManager`.

Useful patterns adopted here:

- Group settings by infrastructure, internal components, and external connections.
- Parse YAML once during process startup.
- Convert YAML nodes into typed configuration objects before starting components.
- Validate required fields and ranges before opening sockets or starting worker threads.
- Pass immutable component-specific configuration into constructors.
- Keep component lifecycle explicit with `Start`/`Stop` or a blocking `Run` boundary.

Patterns intentionally not copied:

- Global singleton configuration managers.
- A service-locator container for ordinary dependencies.
- Platform-specific `zcore`, `zexchange`, shared-memory, or deployer dependencies.
- String-based component factories before multiple implementations actually exist.
- Runtime mutation of unrelated configuration from arbitrary modules.

## Target Layout

```yaml
system:
  name: cockpit-system
  vehicle_id: car_001

paths:
  data_dir: data
  log_dir: logs

logging:
  level: info
  max_bytes: 2097152
  mirror_stderr: true

services:
  vehicle_data:
    source: mock
    publish_interval_ms: 200
    grpc:
      listen_address: 127.0.0.1:50050
  gateway:
    vehicle_data_address: 127.0.0.1:50050
    grpc:
      listen_address: 127.0.0.1:50051
    websocket:
      listen_address: 127.0.0.1:18080
  audio:
    auto_start: false
    grpc:
      listen_address: 127.0.0.1:50052
    vad:
      enabled: true
      backend: energy
      speech_threshold_dbfs: -40.0
      speech_start_frames: 3
      speech_end_frames: 10
    speech_segment:
      pre_roll_ms: 100
      max_segment_ms: 15000
  cloud_uplink:
    enabled: false
    mqtt:
      broker: tcp://127.0.0.1:1883
      telemetry_topic: vehicle/status
      qos: 1

hardware:
  can:
    interface: vcan0
    receive_timeout_ms: 500
  audio:
    capture_backend: alsa
    playback_backend: alsa

features:
  voice:
    enabled: false
    mode: push_to_talk
    asr_provider: mock
    tts_provider: mock
  ai:
    provider: mock

tools:
  topic:
    backend: file
    dir: logs/topics
```

## Typed Ownership

`SystemConfig` is the immutable root loaded by `ServiceRuntime`. It owns typed child structures such
as `VehicleDataConfig`, `GatewayConfig`, `CanConfig`, `AudioConfig`, and `CloudUplinkConfig`.
Binaries consume those objects directly instead of reading dotted string keys.

Configuration errors are startup failures. Missing optional values receive documented defaults;
invalid required addresses, ports, intervals, or enum-like values produce a concrete error naming
the YAML path.

Speech segmentation is bounded to 2 seconds of pre-roll and 60 seconds per segment. Both values
must align with `hardware.audio.frame_ms`, and pre-roll must be shorter than the segment limit.
Voice mode and ASR/TTS providers currently accept only `push_to_talk` and `mock`.

## Implemented

- `core/config/SystemConfig` is loaded once by `ServiceRuntime` through yaml-cpp.
- All service, CAN, MQTT placeholder, logging, and topic settings use typed child structures.
- The former dotted-string getters and separate `configs/logging.yaml` have been removed.
- CTest covers the real configuration and rejects invalid gRPC, audio backend, and VAD threshold
  values with their YAML paths.
