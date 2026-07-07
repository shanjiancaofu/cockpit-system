# cockpit-ui

Jetson 车机端 Qt 6/QML 应用。

## 当前能力

- 后台 gRPC worker 订阅 gateway 车辆状态。
- UI 线程中的 `VehicleStateModel` 提供 live/stale/disconnected 状态。
- Camera 页面通过 gRPC 控制 camera-service 启停。
- `CameraFrameClient` 在后台读取共享内存最新帧。
- `CameraImageProvider` 通过 `image://camera` 向 QML 提供 BGRx/RGB 图像。
- Camera 页面可将当前新鲜帧保存为 JPEG，并展示最后照片路径。
- 相机状态区分 waiting/live/stalled/last-frame/disconnected，并在 writer 重启后重连。

## 运行

```bash
bash scripts/run_cockpit_ui.sh
```

WSL 无显示环境：

```bash
bash scripts/run_cockpit_ui.sh --offscreen
```

脚本会启动需要的 vehicle、gateway 和 camera service。有 `/dev/video0` 时会尝试启动预览。

真实 USB 摄像头联调使用 `bash scripts/run_camera_ui.sh`。该入口要求 `CAMERA_DEVICE` 存在，
并在启动 UI 前确认 camera-service 已收到真实帧。

UI 不直接访问 ALSA、V4L2 或 SocketCAN，所有硬件能力由 service 持有。
