# driver 目录职责

`cockpit/library/driver` 是硬件驱动模块的统一进程入口层，保持为一个完整目录，不按
service 拆分。它负责把底层适配、领域能力和进程生命周期组合起来。

三个相邻层次的职责不同：

| 目录 | 职责 |
|---|---|
| `cockpit/drivers` | ALSA、V4L2、SocketCAN 等低层平台适配 |
| `cockpit/modules` | 与进程入口无关的 audio、camera、vehicle 等领域能力 |
| `cockpit/library/driver` | 动态模块入口、运行时资源、gRPC/IPC 桥接及启动停止 |

当前子目录：

- `audio/`：音频运行时和模块入口。
- `camera/`：相机运行时、录制桥接和模块入口。
- `vehicle/`：车辆运行时、数据服务、gRPC 接口和模块入口。

新增硬件能力时，底层设备访问优先放入 `cockpit/drivers`，可测试的业务逻辑放入
`cockpit/modules`，最后由本目录完成进程级组装。
