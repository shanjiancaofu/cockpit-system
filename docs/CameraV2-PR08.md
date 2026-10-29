# Camera V2 PR-08：H.264 到 RTSP

PR-08 将 Native Argus 的双消费者路径输出的 H.264 Access Unit 通过本地 UDP 分片送入
TransferRuntime，再由 GStreamer RTSP Server 提供给 VLC 等客户端。Transfer 不采集相机，
也不重新编码。

## 本地配置

在 `configs/development.yaml` 中启用：

```yaml
services:
  transfer:
    rtsp_enabled: true
    rtsp_address: 0.0.0.0
    rtsp_port: 8554
    rtsp_path: /camera
    rtsp_udp_port: 8556
    rtsp_fps: 30
```

RTSP 输入 UDP 只绑定 `127.0.0.1:8556`，用于保护内部视频注入端口；相机编码器应使用
`--h264-udp-host 127.0.0.1 --h264-udp-port 8556`。

## Jetson 验收

先启动 Transfer 和 Camera：

```bash
cockpit-navigator --config configs/development.yaml
camera-ctl --start --device nvargus://0 --width 1920 --height 1080 --fps 30 \
  --h264-width 1280 --h264-height 720 --h264-bitrate 2000000 --h264-gop 30 \
  --h264-udp-host 127.0.0.1 --h264-udp-port 8556
```

在 VLC 打开：

```text
rtsp://<jetson-ip>:8554/camera
```

验收至少包括 720p 播放、1080p 播放、Qt 预览并行、客户端断开后重连，以及两个客户端
同时播放。停止并重新启动 Camera 后，客户端应等待下一个 IDR 恢复，不应继续收到旧帧。

## 软件验证

```bash
cmake --build _output/build/arm64-debug-auto -j2
ctest --test-dir _output/build/arm64-debug-auto \
  -R 'h264_encoder|system_config|gateway_grpc_service' --output-on-failure
```

当前仓库验证覆盖编码器、UDP/RTSP 依赖构建、配置和 Transfer Gateway；真实 VLC 播放仍需
在 Jetson 上执行上述验收并记录结果。
