# topic

Local topic debugging tool, inspired by ROS topic commands.

This is a development tool for the current single-Jetson stage. It uses a file-backed local topic
store under `tools.topic.dir` from `configs/config.yaml`.

## Commands

```bash
build/bin/topic list --config configs/config.yaml
build/bin/topic info /vehicle/state --config configs/config.yaml
build/bin/topic pub /vehicle/state '{"speed_kph":12.3}' --config configs/config.yaml
build/bin/topic echo /vehicle/state --tail 5 --config configs/config.yaml
build/bin/topic hz /vehicle/state --window 100 --config configs/config.yaml
```

Messages are stored as JSON lines:

```json
{"timestamp_ms":1781867205540,"topic":"/vehicle/state","payload":{"speed_kph":12.3}}
```

## Scope

Current scope:

- `list`
- `info`
- `pub`
- `echo`
- `echo --follow`
- `hz`
- `hz --follow`

## Source Layout

Each command is a separate translation unit:

```text
main.cc             # dispatch only
topic_list.cc/.h
topic_info.cc/.h
topic_pub.cc/.h
topic_echo.cc/.h
topic_hz.cc/.h
topic_command_line.cc/.h
topic_store.cc/.h
topic_message.cc/.h
topic_text.cc/.h
topic_usage.cc/.h
```

CMake builds command code into `topic_commands` first, then links the `topic` executable. This
keeps commands independently compiled and easy to split later.

Namespace:

```text
cockpit::topic
```

CMake visibility:

- `topic_commands` publishes only its local include directory.
- `topic_commands` links `config` and `utils` for storage and timestamps.
- `topic` links `config` because `main.cc` loads runtime configuration.
- `topic` links `topic_commands` privately.

Future scope:

- connect service publishers to this tool
- add typed topic schemas from protobuf
- add gRPC gateway backend for native services and CLI subscription
- add WebSocket gateway backend for browser dashboard subscription
- keep MQTT for cloud/remote publish and optional remote subscription

## Backend Direction

Current backend:

```text
topic CLI -> file backend -> logs/topics/*.jsonl
```

Recommended runtime backend later:

```text
C++ services/tools -> gRPC streaming -> cockpit-gateway-service
browser dashboard  -> WebSocket      -> cockpit-gateway-service
cloud/remote       -> MQTT           -> cloud-uplink-service
```

So `topic echo` and `topic hz` should eventually subscribe through the gateway gRPC stream on the
Jetson. WebSocket is mainly for browser clients. MQTT is better kept for cloud or remote devices,
not as the first local service bus.
