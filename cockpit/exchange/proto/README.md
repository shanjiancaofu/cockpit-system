# proto

车端进程级模块之间的 protobuf/gRPC 契约：

- `common.proto`：通用状态和空请求。
- `vehicle_state.proto`：周期 VehicleState、正式 ChassisState、CAN 链路健康和离散 typed
  `ChassisEvent` 流。ChassisState 承载 Motion/Odometry/Heartbeat/Fault 聚合结果，不暴露原始 CAN 帧。
- `gateway.proto`：gateway 查询和事件流。
- `audio.proto`：音频控制、指标、transcript 和 Speak。
- `voice.proto`：语音交互状态和回复。
- `hmi.proto`：Qt/Android HMI 控制命令和执行结果。
- `media.proto`：固定 track ID、曲目列表、播放状态和控制。
- `bridge.proto`：Cockpit 到未来 ROS2/Nav2 adapter 的 typed goal、cancel、pose 和状态合同；不包含 ROS 类型。
- `camera.proto`：相机控制和健康状态。
- `recording.proto`：研发录包控制、时间线、完整性和报告。
- `sentinel.proto`：哨兵布防、状态、触发计数和取证结果；不承载原始 CAN 帧。
- `cloud.proto`：云端占位契约，当前没有生产 transport consumer。

CMake 将生成代码放在 build 目录并提供 `contracts` target。禁止手工修改生成文件。
