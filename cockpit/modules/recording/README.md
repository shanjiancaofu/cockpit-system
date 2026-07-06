# recording 模块

提供与 transport 无关的录包会话文件管理：

- 创建 `.recording_<id>` 临时目录。
- 写入 `manifest.json` 和 `vehicle_state.jsonl`。
- 停止时原子改名并创建 `COMPLETE` 标记。
- 启动时识别上次异常中断的临时目录。

本模块不订阅 gRPC、不读取摄像头或音频设备；数据订阅和进程生命周期属于
`cockpit/services/recording-service`。
