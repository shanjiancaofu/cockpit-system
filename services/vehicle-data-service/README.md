# vehicle-data-service

车辆数据源所有者。

支持：

- `mock`：无硬件开发和 smoke。
- `socketcan`：从 `vcan0` 或真实 CAN 接口读取。
- 使用原型 `VehicleCanCodec` 解码车辆状态。
- 通过 gRPC server streaming 发布 VehicleState。

正式车辆接入前必须替换为基于 DBC/信号文档的 codec，并补充错误值、超时和总线故障处理。
