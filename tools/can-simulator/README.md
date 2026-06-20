# can-simulator

## Responsibility

Generate deterministic CAN-like frames for the first vehicle-data-service chain.

The tool supports two backends:

- `stdout`: print deterministic frames without CAN hardware.
- `socketcan`: send real Linux CAN frames through `vcan0` or `can0`.

Example:

```bash
build/bin/can-simulator --backend stdout --samples 3
build/bin/can-simulator --backend socketcan --samples 3
```

Both modes print `candump`-style text:

```text
vcan0 123#0001020304050607
```

## Config

- `hardware.can.interface`
- `hardware.can.simulator_backend`
- `hardware.can.simulator_interval_ms`
- `paths.log_dir`
- `logging.level`
