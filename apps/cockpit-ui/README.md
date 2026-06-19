# cockpit-ui

Qt/QML cockpit UI placeholder.

Current rule:

- QML does not access hardware directly.
- QML talks to C++ models.
- C++ models talk to `cockpit-gateway-service` through gRPC.
- The old `../vehicle-system/src/app` Widgets shell is the nearest UI migration reference.

This target is intentionally not part of the default build until Qt/QML and gRPC client
dependencies are selected for Jetson.
