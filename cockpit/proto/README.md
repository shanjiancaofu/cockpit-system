# proto

车端服务之间的 protobuf/gRPC 契约：

- `common.proto`：通用状态和空请求。
- `vehicle_state.proto`：车辆状态。
- `gateway.proto`：gateway 查询和事件流。
- `audio.proto`：音频控制、指标、transcript 和 Speak。
- `voice.proto`：语音交互状态和回复。
- `hmi.proto`：Qt/Android HMI 控制命令和执行结果。
- `camera.proto`：相机控制和健康状态。
- `recording.proto`：研发录包控制、时间线、完整性和报告。
- `cloud.proto`：云端占位契约。

CMake 将生成代码放在 build 目录并提供 `contracts` target。禁止手工修改生成文件。
