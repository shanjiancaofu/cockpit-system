# camera-ctl

Local gRPC diagnostic client for `camera-service`.

```bash
build/bin/camera-service --config configs/config.yaml
build/bin/camera-ctl --status --config configs/config.yaml
build/bin/camera-ctl --list --config configs/config.yaml
build/bin/camera-ctl --start --device /dev/video0 --width 640 --height 480 --fps 30 \
  --config configs/config.yaml
build/bin/camera-ctl --stop --config configs/config.yaml
```

`camera-ctl` controls preview lifecycle only. Video frames are not transported over gRPC; future
camera data paths should use a local pipeline or shared memory.
