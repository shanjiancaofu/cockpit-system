# Camera V2 PR-07：H.264 软件编码第一闭环

PR-07 当前实现了最小的 `CpuVideoFrame(I420) → libavcodec/libx264 → Annex-B H.264` 组件，编码器使用 `ultrafast + zerolatency`、无 B-frame、GOP 30，并提供有界的原始 H.264 文件写入器。

编码器位于 `cockpit/driver/camera/encode/`。Native 硬件 Gate 复用 `NativeArgusCapture`、`NativeFrameBroker` 和 VIC Transform，先将 Transform Lease 转成 CPU 自有 I420 数据并释放 Lease，再交给 libx264；编码阶段不会持有 DMA-BUF。

## 验证

```bash
cmake --build _output/build/arm64-debug --target h264_encoder_test native_h264_capture_hardware_test -j4
ctest --test-dir _output/build/arm64-debug -R '^h264_encoder_test$' --output-on-failure

_output/build/arm64-debug/bin/native_h264_capture_hardware_test \
  300 /tmp/cockpit-pr07-720p.h264 1280 720
gst-launch-1.0 -q filesrc location=/tmp/cockpit-pr07-720p.h264 ! h264parse ! nvv4l2decoder ! fakesink sync=false

_output/build/arm64-debug/bin/native_h264_capture_hardware_test \
  300 /tmp/cockpit-pr07-1080p.h264 1920 1080
gst-launch-1.0 -q filesrc location=/tmp/cockpit-pr07-1080p.h264 ! h264parse ! nvv4l2decoder ! fakesink sync=false
```

2026-10-27 真机结果：720P 300 帧、300 packets、约 2.56 MB、10 个 IDR，编码器报告约 2.05 Mbps；1080P 300 帧、300 packets、约 5.10 MB、10 个 IDR，编码器报告约 4.08 Mbps。两路均被解析为 Constrained Baseline、30 FPS，NVIDIA 解码链路退出码为 0。

当前 Gate 是独立编码闭环；下一步是把 Transform Consumer 接入持续运行的 Native Preview，验证 Qt 预览和编码并行时的有界背压、实际输入/输出 FPS、CPU、RSS、P95 和温度。

2026-10-27 已完成第一轮 CameraService 双消费者验证：通过 `camera-ctl --start --h264-output` 启动 1920×1080 Native Preview，同时输出 1280×720 H.264；约 264 帧预览无丢帧，输出文件约 2.2MB，`nvv4l2decoder` 解码链路退出码为 0。编码线程在 VIC I420 转换并释放 Transform Lease 后运行。

示例：

```bash
camera-ctl --start --device nvargus://0 --width 1920 --height 1080 --fps 30 \
  --h264-output /tmp/camera.h264 --h264-width 1280 --h264-height 720 \
  --h264-bitrate 2000000 --h264-gop 30
```
