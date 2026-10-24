# vehicle 模块

包含平台无关的 `VehicleState`、原型 `VehicleCanCodec` 和正式底盘
`ChassisCanCodec`。

当前 codec 只用于项目联调，不代表正式车辆协议。接入真实车辆前必须根据确认后的 DBC 或信号文档
实现 ID、bit、scale、offset、endianness、有效性和故障值处理。

`ChassisCanCodec` 是例外：它实现 chassis-controller 已冻结的 CAN FD 1.0 契约，包括正式
`0x101` 速度控制、双向 heartbeat、`0x180/0x181/0x240` 状态解码，以及独立的可选开发握手。
字段权威源位于
`chassis-controller/protocol/chassis_canfd.yaml`，修改 codec 时必须用两端 golden vector 回归。
Cockpit 侧的精确 repository revision、文件和 SHA-256 固定在
`configs/chassis-protocol-pins.env`；修改 codec 或协议前先运行
`bash scripts/dev/verify-chassis-protocol.sh`，禁止只凭两个仓库都能编译就认定合同一致。

`ChassisClient` 在 codec 之上聚合 Motion、Odometry、Heartbeat 和 Fault，维护 STM32 peer
`UNKNOWN/ALIVE/TIMEOUT`，并生成 Jetson 100 ms heartbeat。它输出平台无关的 `ChassisState`，
不持有 SocketCAN、gRPC 或 Navigator 生命周期，也暂不开放运动控制 RPC。0x240 frame sequence 只接受
uint8 delta 1..127；重复/旧帧不覆盖 fault，只有 peer timeout 或 heartbeat uptime 确认 reboot 才重建
baseline。reboot 通过单调 `peer_reboot_count` 传播，供 odometry 时间映射与 Safety fail-closed 使用。
