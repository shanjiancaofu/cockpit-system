# cockpit-ctl

`cockpit-ctl` is the top-level local diagnostics CLI for cockpit-system.

```bash
# 一次性查询 / one-shot query
build/bin/cockpit-ctl status --config configs/config.yaml

# 持续刷新 / watch mode (Ctrl+C to stop)
build/bin/cockpit-ctl status --watch --config configs/config.yaml

# 自定义刷新间隔（秒）/ custom refresh interval (seconds)
build/bin/cockpit-ctl status --watch --interval 3 --config configs/config.yaml
```

Current commands:

- `status`: query gateway, audio-service, voice-interaction-service, and camera-service through
  their existing gRPC control APIs.
- `status --watch`: refresh status periodically. Default interval is 2 seconds.
- `status --watch --interval N`: set custom refresh interval (minimum 1 second).

The tool is intentionally read-only for now. Runtime module restart/control can be added after the
module status surface is stable.
