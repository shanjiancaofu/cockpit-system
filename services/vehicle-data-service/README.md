# vehicle-data-service

## Responsibility

Own vehicle state ingestion and normalization.

Current:

- Read mock or SocketCAN input.
- Decode the prototype VehicleState CAN frame.
- Publish JSON for diagnostics and a gRPC VehicleState stream.

Later phases:

- Replace the prototype CAN mapping with an approved DBC.
- Decode additional chassis and sensor fields.

## Input

- `configs/config.yaml`
- SocketCAN frames from `hardware.can.interface`

## Output

- JSON lines, service log, and `cockpit.proto.vehicle.VehicleState` gRPC stream.

## Config

- `system.vehicle_id`
- `services.vehicle_data.source`
- `services.vehicle_data.publish_interval_ms`
- `services.vehicle_data.grpc.listen_address`
- `hardware.can.interface`
- `paths.log_dir`
- `logging.level`
