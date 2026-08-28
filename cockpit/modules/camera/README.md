# camera 模块

```text
camera/
├── frames/         CameraFrame、sink、latest-frame buffer
├── capture/        Argus/GStreamer、Direct V4L2 和合成 preview source
└── shared_memory/  跨进程双缓冲
```

对应 target：`camera_frames`、`camera_capture`、`camera_shm`。父级 `camera` 是轻量聚合。

相机后端保持明确分层：

```text
capture_backend: synthetic
  → SyntheticPreviewSource

capture_backend: gstreamer 或 argus
  → GstreamerPreviewPipeline
  → nvarguscamerasrc / NVIDIA ISP
  → BGRx/RGB CameraFrame

capture_backend: v4l2
  → V4l2PreviewSource
  → V4l2MmapCapture
  → ioctl/MMAP/poll
  → RG10 Bayer CameraFrame
```

`gstreamer` 和 `argus` 都表示 Jetson Argus/GStreamer 路径；`v4l2` 表示 Direct V4L2 userspace 路径，不再通过 `v4l2src` 混入 GStreamer。V4L2 backend 经 `SoftwareIsp` 输出 BGRx；底层诊断 CLI 仍可保留原始 RG10。Software ISP 使用预分配 buffer、NEON unpack 和采集/ISP 双线程有界队列，当前 1920×1080@30 实测 0 丢帧。两条路径可以访问同一个 IMX219，但不能同时占用同一个 sensor session。

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

CUDA ISP 仅作为性能 prototype；它的 H2D/kernel/D2H 速度优于 CPU，但与 CPU OpenCV demosaic 的红蓝通道尚未完全对齐，不作为默认 backend。

`SyntheticPreviewSource` 提供不依赖摄像头硬件的 BGRx 帧，并支持 no-frames、stall 和 disconnect
故障注入；它只用于开发验证和稳定性测试，不替代 Jetson 实机 pipeline 验证。
