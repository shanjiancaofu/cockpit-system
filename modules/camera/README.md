# camera 模块

```text
camera/
├── frames/         CameraFrame、sink、latest-frame buffer
├── capture/        preview source 和可选 GStreamer pipeline
└── shared_memory/  跨进程双缓冲
```

对应 target：`camera_frames`、`camera_capture`、`camera_shm`。父级 `camera` 是轻量聚合。

预览 callback 通过 move 传递 frame。`LatestFrameBuffer` 只保存最新帧，不形成无界视频队列。
共享内存使用两个槽位，writer 写入非活动槽后切换 generation，图片像素不经过 gRPC。

安装 GStreamer development package 后会构建 `gstreamer_camera`：

```bash
build/bin/camera-preview-probe --device /dev/video0 --frames 30 --config configs/config.yaml
```
