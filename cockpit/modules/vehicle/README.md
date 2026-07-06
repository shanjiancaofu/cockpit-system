# vehicle 模块

包含平台无关的 `VehicleState` 和原型 `VehicleCanCodec`。

当前 codec 只用于项目联调，不代表正式车辆协议。接入真实车辆前必须根据确认后的 DBC 或信号文档
实现 ID、bit、scale、offset、endianness、有效性和故障值处理。
