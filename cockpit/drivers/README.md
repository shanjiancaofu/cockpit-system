# drivers

Linux 和硬件适配层：

- `alsa`：音频采集和播放。
- `socketcan`：CAN socket。
- `v4l2`：摄像头设备发现和 capability 查询。

driver 将平台 API 适配为 module 接口，不包含语音、UI 或车辆业务策略。
