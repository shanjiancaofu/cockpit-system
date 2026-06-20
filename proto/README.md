# proto

Protobuf contracts for local gRPC and future cloud transport.

- Packages use `cockpit.proto.*` so generated transport classes do not collide with domain models
  under `cockpit::*`.
- CMake generates C++ and gRPC sources into `build/proto/generated`.
- Consumers link the `contracts` target; generated files are never committed.
- Existing field numbers must not be reused or renumbered after a contract is released.

Current local endpoint:

```text
vehicle-data-service  127.0.0.1:50050  VehicleDataService/SubscribeVehicleState
cockpit-gateway       127.0.0.1:50051  CockpitGateway/ListTopics
cockpit-gateway       127.0.0.1:50051  CockpitGateway/GetTopicInfo
cockpit-gateway       127.0.0.1:50051  CockpitGateway/SubscribeCockpitEvents
```

The connection currently uses insecure credentials because it is limited to local vehicle-side
service communication. Remote interfaces require an explicit authentication and TLS design.
