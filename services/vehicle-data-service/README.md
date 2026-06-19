# vehicle-data-service

## Responsibility

Own vehicle state ingestion and normalization.

First phase:

- Generate mock `VehicleState`.
- Keep the future SocketCAN/vcan0 boundary explicit.
- Publish a JSON line for local smoke runs.

Later phases:

- Read SocketCAN from `vcan0` or `can0`.
- Decode speed, gear, SOC, and selected sensor fields.
- Expose `VehicleDataService.SubscribeVehicleState` through gRPC.

## Input

- `configs/config.yaml`
- Future: SocketCAN frames from `can.interface`

## Output

- Current: JSON lines and service log.
- Future: gRPC stream `cockpit.vehicle.VehicleState`.

## Config

- `vehicle.id`
- `vehicle.publish_interval_ms`
- `can.interface`
- `logging.dir`
- `logging.level`
