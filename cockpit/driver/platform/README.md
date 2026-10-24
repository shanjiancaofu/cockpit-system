# drivers

Linux 和硬件适配层：

- `alsa`：音频采集和播放。
- `socketcan`：CAN socket。
- `v4l2`：摄像头设备发现和 capability 查询。

driver 直接封装平台 API 和硬件原始数据，只依赖 STL、libc 与对应 Linux/ALSA/V4L2/SocketCAN
系统接口，不依赖 `cockpit/core`、`cockpit/modules`、`cockpit/library`、Navigator 或 `agent/`。
领域类型和 driver 原始类型之间的转换由对应 module 完成。

本目录不使用 `Service` 命名。硬件对象按职责命名为 `Pcm`、`Camera` 或 `SocketCan`；进程、
RPC、AudioFrame、VehicleState 和应用语义属于上层。
