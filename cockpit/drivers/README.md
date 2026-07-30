# drivers

Linux 和硬件适配层：

- `alsa`：音频采集和播放。
- `socketcan`：CAN socket。
- `v4l2`：摄像头设备发现和 capability 查询。

driver 将平台 API 适配为 module 接口，不包含语音、UI 或车辆业务策略。

本目录不使用 `Service` 命名。硬件对象按职责命名为 `Pcm`、`CaptureSource`、
`AudioPlayer`、`Camera` 或 `SocketCan`；进程、RPC 和应用语义属于上层。
