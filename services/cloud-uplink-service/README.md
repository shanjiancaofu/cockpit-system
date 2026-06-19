# cloud-uplink-service

## Responsibility

Bridge local vehicle state to cloud protocol.

First phase:

- Load MQTT/cloud config.
- Convert a mock `VehicleState` to the planned cloud telemetry shape.
- Keep MQTT integration as an explicit placeholder.

Later phases:

- Subscribe to cockpit gateway or vehicle data summaries.
- Publish MQTT protobuf telemetry.
- Pull platform/vehicle config according to the car cloud API.
- Handle reconnect, heartbeat, offline buffering, and command ACK.

## Input

- Vehicle state summary.
- Cloud config and credentials.

## Output

- MQTT protobuf telemetry.
- Future config pull/download requests.

## Config

- `vehicle.id`
- `mqtt.broker`
- `mqtt.telemetry_topic`
- `mqtt.qos`
- `logging.dir`
- `logging.level`
