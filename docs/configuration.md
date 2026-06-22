# Configuration Architecture

## Build Configuration

The project requires C++17 or newer and defaults to C++17. A newer standard can be selected at
configure time without editing the repository:

```bash
cmake -S . -B build -DCMAKE_CXX_STANDARD=20
```

Values below C++17 are rejected during CMake configuration.

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

## Implemented

- `core/config/SystemConfig` is loaded once by `ServiceRuntime` through yaml-cpp.
- All service, CAN, MQTT placeholder, logging, and topic settings use typed child structures.
- The former dotted-string getters and separate `configs/logging.yaml` have been removed.
- CTest covers the real configuration and rejects invalid gRPC and audio backend values with their
  YAML paths.
