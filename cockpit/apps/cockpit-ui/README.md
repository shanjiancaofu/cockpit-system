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
- 本地 HMI gRPC 接收受控动作；`open_camera_preview` 在 Qt 主线程切换 Camera 页面并返回执行结果。
- 媒体播放器尚未接入，`play_music` 明确返回未执行。

## 运行

```bash
bash scripts/run_cockpit_ui.sh
```

WSL 无显示环境：

```bash
bash scripts/run_cockpit_ui.sh --offscreen
```

脚本只启动 Navigator；normal mode 中的 HMI Runtime 负责启动和监管 `cockpit-ui`。有
`/dev/video0` 时脚本会尝试启动预览，Ctrl+C 会由 Navigator 统一回收 UI 和业务模块。

真实 USB 摄像头联调使用 `bash scripts/run_camera_ui.sh`。该入口要求 `CAMERA_DEVICE` 存在，
并在启动 UI 前确认 camera-service 已收到真实帧。

UI 不直接访问 ALSA、V4L2 或 SocketCAN，所有硬件能力由对应 driver module 持有。
