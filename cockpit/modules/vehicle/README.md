# vehicle 模块

包含平台无关的 `VehicleState`、原型 `VehicleCanCodec` 和正式底盘
`ChassisCanCodec`。

当前 codec 只用于项目联调，不代表正式车辆协议。接入真实车辆前必须根据确认后的 DBC 或信号文档
实现 ID、bit、scale、offset、endianness、有效性和故障值处理。

`ChassisCanCodec` 是例外：它实现 chassis-controller 已冻结的 CAN FD 1.0 契约，包括正式
`0x101` 速度控制、双向 heartbeat、`0x180/0x181/0x240` 状态解码，以及独立的可选开发握手。
字段权威源位于
`chassis-controller/protocol/chassis_canfd.yaml`，修改 codec 时必须用两端 golden vector 回归。
