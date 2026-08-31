# library 目录职责

`cockpit/library` 保存由 Navigator 装载的进程级模块。这里的代码负责组合领域能力、
持有模块运行时资源，并提供统一的启动、停止、轮询和 IPC 接口；它不是通用工具库，
也不是独立的 service 层。

主要目录：

- `driver/`：硬件相关模块的进程入口与运行时生命周期。
- `agent/`：语音交互和后续 AI 能力的编排入口。
- `hmi/`：HMI 进程级运行时。
- `recording/`：运行数据记录。
- `sentinel/`：消费 typed 底盘事件并编排相机取证与录包，不解析原始 CAN。
- `bridge/`：持有 Bridge gRPC/runtime；当前只组装 disabled/fake provider，不链接 ROS2。
- `transfer/`、`carupload/`：数据传输与上传。
- `calibration/`、`debugger/`、`upgrader/`、`watchdog/`：对应的进程级能力。

目录边界：

- 可复用的领域逻辑放在 `cockpit/modules`。
- ALSA、V4L2、SocketCAN 等底层平台适配放在 `cockpit/drivers`。
- 只有需要被 Navigator 独立装载、管理生命周期或暴露 IPC 的组合代码放在这里。

Vehicle driver 持有单一 SocketCAN 生命周期：发送 Jetson heartbeat，使用
`SO_TIMESTAMPNS/recvmsg` 保留 kernel RX time 并映射到 steady freshness，再把 CAN FD 帧交给
`modules/vehicle/ChassisClient`，并通过现有 Vehicle gRPC service 发布强类型 `ChassisState`。
SocketCAN poll timeout 周期仍会推进 `ChassisClient`，因此 peer 完全静默时也会主动发布 heartbeat
`ALIVE→TIMEOUT`，不等待下一帧触发。
不另建平行 CAN service，也不在 driver 中重复协议字段定义。
命令 sink 保留两个显式工厂：`OpenVcanOnly("vcan0")` 仅供 VM，`OpenHardware("can0")` 仅供真实
硬件装配；当前 wheels-up 前不得由 Nav2/runtime 自动调用 hardware 工厂。
