# cockpit-gateway-service

## Responsibility

Aggregate local service state and provide stable UI/debug interfaces.

Current:

- Load config and initialize logging.
- Subscribe to the vehicle-data gRPC stream.
- Retry interrupted streams and discard duplicate timestamps.
- Serve `CockpitGateway.SubscribeCockpitEvents` to local clients.
- Forward the latest VehicleState as a typed CockpitEvent stream.
- Preserve the downstream WebSocket boundary.

Later phases:

- Connect `CockpitGateway.SubscribeCockpitEvents` to Qt/QML.
- Provide WebSocket JSON state for `apps/web-dashboard`.
- Apply throttling and filtering before UI delivery.

## Input

- gRPC streams from local services.
- UI control requests.

## Output

- gRPC stream to cockpit UI.
- WebSocket JSON stream to browser dashboard.

## Config

- `services.gateway.vehicle_data_address`
- `services.gateway.stream_timeout_ms`
- `services.gateway.retry_delay_ms`
- `services.gateway.grpc.listen_address`
- `services.gateway.websocket.listen_address`
- `paths.log_dir`
- `logging.level`
