# camera-ctl

camera-service 的本地 gRPC 诊断客户端。

```bash
build/bin/camera-ctl --list --config configs/config.yaml
build/bin/camera-ctl --status --config configs/config.yaml
build/bin/camera-ctl --start --device /dev/video0 --width 640 --height 480 --fps 30 \
  --config configs/config.yaml
build/bin/camera-ctl --stop --config configs/config.yaml
```

工具只控制预览生命周期并查看状态，不传输视频帧。
