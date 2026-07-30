# driver 目录职责

`cockpit/library/driver` 是硬件驱动模块的统一进程入口层，保持为一个完整目录，不按
业务 service 拆分。它负责把底层适配、数据传输和进程生命周期组合起来。

三个相邻层次的职责不同：

| 目录 | 职责 |
|---|---|
| `cockpit/drivers` | ALSA、V4L2、SocketCAN 等低层平台适配 |
| `cockpit/modules` | 与进程入口无关的 PCM、图像帧、车辆状态等领域协议 |
| `cockpit/library/driver` | 动态模块入口、运行时资源、gRPC/IPC 桥接及启动停止 |

当前子目录：

- `audio/`：音频设备运行时、PCM 传输和模块入口。
- `camera/`：相机运行时、录制桥接和模块入口。
- `vehicle/`：车辆运行时、数据服务、gRPC 接口和模块入口。

新增硬件能力时，底层设备访问优先放入 `cockpit/drivers`，中立数据协议放入
`cockpit/modules`，最后由本目录完成进程级组装。VAD、ASR、TTS、LLM 和对话策略属于
顶层 `agent/`，不能进入本目录。

本目录自有类型使用 `Runtime`、`Controller`、`Provider`、`Publisher` 或 `GrpcServer`
等明确名称，避免使用含义宽泛的 `Service`。gRPC 生成基类中的 `Service` 名称除外。
