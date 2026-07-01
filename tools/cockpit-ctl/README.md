# cockpit-ctl

`cockpit-ctl` is the top-level local diagnostics CLI for cockpit-system.

```bash
build/bin/cockpit-ctl status --config configs/config.yaml
```

Current commands:

- `status`: query gateway, audio-service, voice-interaction-service, and camera-service through
  their existing gRPC control APIs.

The tool is intentionally read-only for now. Runtime module restart/control can be added after the
module status surface is stable.
