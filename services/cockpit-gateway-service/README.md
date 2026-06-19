# cockpit-gateway-service

## Responsibility

Aggregate local service state and provide stable UI/debug interfaces.

First phase:

- Load config and initialize logging.
- Emit a mock vehicle snapshot.
- Preserve the gRPC/WebSocket boundary.

Later phases:

- Subscribe to `vehicle-data-service`.
- Provide `CockpitGateway.SubscribeCockpitEvents` for Qt/QML.
- Provide WebSocket JSON state for `apps/web-dashboard`.
- Apply throttling and filtering before UI delivery.

## Input

- gRPC streams from local services.
- UI control requests.

## Output

- gRPC stream to cockpit UI.
- WebSocket JSON stream to browser dashboard.

## Config

- `gateway.grpc_port`
- `gateway.websocket_port`
- `logging.dir`
- `logging.level`
