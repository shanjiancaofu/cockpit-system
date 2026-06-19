# can-simulator

## Responsibility

Generate deterministic CAN-like frames for the first vehicle-data-service chain.

Current behavior prints mock frames in `candump`-style text:

```text
vcan0 123#0001020304050607
```

Later this tool can switch to real SocketCAN send path on Linux.

## Config

- `can.interface`
- `can.simulator_interval_ms`
- `logging.dir`
- `logging.level`

