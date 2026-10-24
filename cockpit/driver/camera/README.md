# camera 模块

```text
camera/
├── frames/         CameraFrame、sink、latest-frame buffer
├── capture/        Argus ISP、USB UVC、Software ISP 和合成 preview source
└── shared_memory/  跨进程双缓冲
```

对应 target：`camera_frames`、`camera_capture`、`camera_shm`。父级 `camera` 是轻量聚合。

产品配置选择完整 pipeline，不允许采集 API、设备类型与 ISP 随机组合：

```text
capture_pipeline: synthetic
  → SyntheticPreviewSource

capture_pipeline: argus_isp
  → ArgusIspPreviewSource
  → internal GstreamerPreviewPipeline
  → nvarguscamerasrc / NVIDIA Hardware ISP
  → BGRx/RGB CameraFrame

capture_pipeline: uvc
  → UvcPreviewSource
  → internal GstreamerPreviewPipeline
  → v4l2src
  → MJPEG decode 或 YUYV conversion
  → BGRx/RGB CameraFrame

capture_pipeline: software_isp
  → SoftwareIspPreviewSource
  → V4l2MmapCapture
  → ioctl/MMAP/poll
  → RG10 Bayer RawBayerFrame
  → SoftwareIsp
  → BGRx CameraFrame
```

`argus_isp` 只接受 `nvargus://N`；`uvc` 只接受 `uvcvideo` USB 节点并显式选择
`mjpeg|yuyv`；`software_isp` 只接受 `tegra-video + RG10`。普通 USB UVC 相机通常已在设备内部完成 Bayer ISP，不再进入 `SoftwareIsp`。Software ISP 使用预分配 buffer、NEON unpack 和采集/ISP 双线程有界队列，公开输出始终是 BGRx；底层诊断 CLI 才保留 RG10。Argus ISP 与 Software ISP 可以访问同一颗 IMX219，但不能同时占用 sensor session。

Jetson + IMX219 的 ARM64/NEON 合入门禁在真机执行：

```bash
scripts/tests/jetson-camera-gate.sh
```

该门禁会原生构建 ARM64 Debug、运行 `raw10_unpack_test`，再通过 `/dev/video0` 执行
`RG10 → NEON RAW unpack → Software ISP` 实际采集。设备、分辨率、帧率和帧数可通过
`JETSON_CAMERA_DEVICE`、`JETSON_CAMERA_WIDTH`、`JETSON_CAMERA_HEIGHT`、
`JETSON_CAMERA_FPS`、`JETSON_CAMERA_FRAMES` 覆盖。

预览 callback 通过 move 传递 frame。`LatestFrameBuffer` 只保存最新帧，不形成无界视频队列。
共享内存使用两个槽位，writer 写入非活动槽后切换 generation，图片像素不经过 gRPC。

UVC MJPEG smoke：

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/camera-preview-probe \
  --backend uvc --uvc-input-format mjpeg --device /dev/video0 \
  --frames 30 --config configs/development.yaml
```

Jetson CSI Argus smoke：

```bash
_output/build/arm64-debug/bin/camera-preview-probe \
  --backend argus --device nvargus://0 --frames 30 \
  --config configs/development.yaml
```

CUDA ISP 仅作为性能 prototype；它的 H2D/kernel/D2H 速度优于 CPU，但与 CPU OpenCV demosaic 的红蓝通道尚未完全对齐，不作为默认 backend。

`SyntheticPreviewSource` 提供不依赖摄像头硬件的 BGRx 帧，并支持 no-frames、stall 和 disconnect
故障注入；它只用于开发验证和稳定性测试，不替代 Jetson 实机 pipeline 验证。
