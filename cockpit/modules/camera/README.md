# camera 模块

```text
camera/
├── frames/         CameraFrame、sink、latest-frame buffer
├── capture/        preview source、可选 GStreamer pipeline 和合成故障源
└── shared_memory/  跨进程双缓冲
```

对应 target：`camera_frames`、`camera_capture`、`camera_shm`。父级 `camera` 是轻量聚合。

预览 callback 通过 move 传递 frame。`LatestFrameBuffer` 只保存最新帧，不形成无界视频队列。
共享内存使用两个槽位，writer 写入非活动槽后切换 generation，图片像素不经过 gRPC。

安装 GStreamer development package 后会构建 `gstreamer_camera`：

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/camera-preview-probe \
  --device /dev/video0 --frames 30 --config configs/development.yaml
```

Jetson CSI 通过 Argus/NVMM 获取 ISP 处理后的帧，不把 V4L2 暴露的 Bayer `RG10` 当作普通
`video/x-raw`。设备使用显式 URI，USB 摄像头路径保持不变：

```bash
_output/build/arm64-debug/bin/camera-preview-probe \
  --device nvargus://0 --frames 30 --config configs/development.yaml
```

`SyntheticPreviewSource` 提供不依赖摄像头硬件的 BGRx 帧，并支持 no-frames、stall 和 disconnect
故障注入；它只用于开发验证和稳定性测试，不替代 Jetson 实机 pipeline 验证。
