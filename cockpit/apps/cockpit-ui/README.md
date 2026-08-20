# cockpit-ui

Jetson 车机端 Qt 6/QML 应用。

## 当前能力

- 1280×720 用户车机主框架，包含顶部状态、驾驶首页和底部应用 Dock。
- 首页只展示真实车辆状态；导航、媒体和 Android/投屏后端未接入时明确显示不可用，不伪造功能。
- 相机、媒体、小山语音、设置和诊断拆分为独立页面；诊断信息不再占用驾驶首页。
- `VoiceStatusModel` 后台读取真实 Voice gRPC 状态、最新 transcript/回复、播放可用性和错误；小山页
  可通过固定 `Interrupt` RPC 取消活动语音请求，不能构造其他 Agent 操作。
- `AppLauncherModel` 固定允许 `local_media`、`phone_projection`、`android_apps` 三个产品 ID，提供
  unavailable/available/starting/running/stopping/failed 状态、重复请求合并和退出回收。接口不接受
  executable、argv 或 shell；当前默认后端全部不可用。
- `MediaControlModel` 提供 stopped/playing/paused/error 状态和 play/pause/resume/next/stop 控制；
  backend 只接收固定 `default_track` ID，不接受路径或 URL。正式 UI 已通过有界 gRPC client 连接
  `media-service`；disabled provider 仍明确显示不可用，mock provider 仅用于生命周期测试。
- 后台 gRPC worker 订阅 gateway 车辆状态。
- UI 线程中的 `VehicleStateModel` 提供 live/stale/disconnected 状态。
- Camera 页面通过 gRPC 调用 `camera_driver` 提供的 CameraControl 接口。
- `CameraFrameClient` 在后台读取共享内存最新帧。
- `CameraImageProvider` 通过 `image://camera` 向 QML 提供 BGRx/RGB 图像。
- Camera 页面可将当前新鲜帧保存为 JPEG，并展示最后照片路径。
- 相机状态区分 waiting/live/stalled/last-frame/disconnected，并在 writer 重启后重连。
- 本地 HMI gRPC 接收受控动作；`open_camera_preview` 在 Qt 主线程切换 Camera 页面并返回执行结果。
- 媒体 gRPC 控制面已接入 UI，但尚未接入真实播放 owner；`play_music` 仍明确返回未执行。
- `cockpit_ui_qml_test` 使用 Qt offscreen/software backend 加载完整 QML 资源并检查页面导航；设置
  `COCKPIT_UI_QML_SCREENSHOT` 可在有显示服务的开发机输出 1280×720 截图用于视觉检查。

## UI 边界

UI 只消费 Vehicle、Camera、Health、HMI 和 Voice 模型，不直接访问 ALSA、V4L2、SocketCAN、ROS 或 shell。
真实本地媒体 owner、音频焦点和 ROS2/Nav2 地图仍是后续接口；gRPC 控制面不等于真实音频播放。
Voice 页面显示的是 200 ms 本地状态快照；协议没有状态事件流，因此极短的中间状态不保证逐帧呈现。

## 运行

```bash
bash scripts/run-cockpit-ui.sh
```

WSL 无显示环境：

```bash
bash scripts/run-cockpit-ui.sh --offscreen
```

脚本只启动 Navigator；normal mode 中的 HMI Runtime 负责启动和监管 `cockpit-ui`。有
`/dev/video0` 时脚本会尝试启动预览，Ctrl+C 会由 Navigator 统一回收 UI 和业务模块。

真实 USB 摄像头联调使用 `bash scripts/run-camera-ui.sh`。该入口要求 `CAMERA_DEVICE` 存在，
并在启动 UI 前确认 `camera_driver` 已收到真实帧。

UI 不直接访问 ALSA、V4L2 或 SocketCAN，所有硬件能力由对应 driver module 持有。
