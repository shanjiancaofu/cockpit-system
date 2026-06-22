# cockpit-ui

Qt 6/QML cockpit UI for the local Jetson display.

Current rule:

- QML does not access hardware directly.
- QML talks to C++ models.
- C++ models talk to `cockpit-gateway-service` through gRPC.
- The old `../vehicle-system/src/app` Widgets shell is the nearest UI migration reference.

Current implementation:

- `VehicleStateModel` exposes connection, speed, gear, SOC, cloud, and source properties to QML.
- `GatewayClient` subscribes to `CockpitGateway.SubscribeCockpitEvents` on a worker thread.
- Model updates cross into the Qt UI thread through queued invocations.
- The dashboard shows live VehicleState data and reconnect status.

The target remains optional so headless service builds do not require Qt:

```bash
cmake -S . -B build -G Ninja -DBUILD_COCKPIT_UI=ON
cmake --build build
build/bin/cockpit-ui --config configs/config.yaml
```

## Qt baseline

- Qt 5 is not supported by this target.
- Ubuntu 22.04 provides Qt 6.2.4 as the WSL build baseline.
- Jetson release images should use one pinned Qt 6.8 LTS toolchain for both development and
  deployment instead of mixing distribution Qt packages and custom Qt builds.
- For a custom Qt installation, pass its prefix explicitly:

```bash
cmake -S . -B build -G Ninja -DBUILD_COCKPIT_UI=ON \
  -DCMAKE_PREFIX_PATH=/opt/qt/6.8/gcc_arm64
```
